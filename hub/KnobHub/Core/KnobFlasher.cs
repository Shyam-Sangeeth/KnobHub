using System.IO;
using System.IO.Compression;
using System.IO.Ports;
using System.Security.Cryptography;
using System.Text;

namespace KnobHub.Core;

/// <summary>
/// Installs a Knob OS image on a knob plugged in over USB, through the ESP32-S3's ROM loader, using Espressif's
/// documented serial protocol (docs.espressif.com, esptool "Serial Protocol"): SLIP-framed commands, the ROM's
/// 4-byte status, compressed (zlib) flash writes and an MD5 check of what was written.
///
/// The image (knob-os.bin, from knob-os/scripts/merged.py) starts at 0x0 and holds the bootloader, the partition table,
/// boot_app0 and the firmware. Everything in it is written except the nvs partition, where Knob OS keeps the knob's
/// pairing key and Wi-Fi network; the file system with its settings lies past the image's end, so it's kept too.
///
/// No buttons are needed. The loader runs on the ESP32-S3's USB Serial/JTAG unit, which resets the chip from the port's
/// DTR and RTS lines: the knob drops off USB, comes back in the loader, and after writing is reset to start Knob OS.
/// Knob OS (from 1.0.2) has a USB serial port of its own instead (see knob-os/platformio.ini), which restarts the chip
/// into the loader when it's opened at 1200 baud; the loader then comes back on another port, the unit's.
/// </summary>
internal static class KnobFlasher
{
    /// <summary>One step of an install, for the progress line: what's happening and how far it is (0-1).</summary>
    public readonly record struct Progress(string Step, double Fraction);

    private const int BlockSize = 0x400;  // the ROM loader's flash write block
    private const int SectorSize = 0x1000;
    private const uint Esp32S3Magic = 0x9;  // the chip id register (0x40001000) on an ESP32-S3

    // Commands (Serial Protocol, "Supported by Stub Loader and ROM Loader").
    private const byte Sync = 0x08, WriteReg = 0x09, ReadReg = 0x0A, SpiSetParams = 0x0B, SpiAttach = 0x0D,
        DeflBegin = 0x10, DeflData = 0x11, SpiFlashMd5 = 0x13;

    // ESP32-S3 RTC registers (ESP-IDF's soc/esp32s3/register/soc/rtc_cntl_reg.h, DR_REG_RTCCNTL_BASE 0x60008000).
    private const uint RtcWdtConfig0 = 0x60008098, RtcWdtProtect = 0x600080B0, WdtKey = 0x50D83AA1;
    private const uint SwdConf = 0x600080B4, SwdProtect = 0x600080B8, SwdKey = 0x8F1D312A, SwdAutoFeed = 1u << 31;
    private const uint Option1 = 0x6000812C, ForceDownloadBoot = 1u << 0;

    /// <summary>Writes <paramref name="image"/> to the knob on <paramref name="portName"/> and restarts it. Blocking:
    /// run it on a worker. Throws with a message for the user when something goes wrong.</summary>
    public static void Flash(string portName, byte[] image, IProgress<Progress> progress, CancellationToken cancel)
    {
        var regions = Regions(image);
        progress.Report(new("Restarting the knob into its loader", 0));
        portName = ResetIntoLoader(portName);
        using var loader = Loader.Open(portName, cancel);
        loader.Connect();
        if (loader.ReadRegister(0x40001000) != Esp32S3Magic)
            throw new IOException("The device on this port isn't an ESP32-S3, so Knob OS can't be installed on it.");
        loader.KeepWatchdogsQuiet();
        loader.Command(SpiAttach, new byte[8]);
        int sizeCode = image.Length > 3 ? image[3] >> 4 : 4;  // the bootloader header's flash size: 0 = 1 MB ... 4 = 16 MB
        loader.Command(SpiSetParams, Words(0, 1u << (20 + sizeCode), 64 * 1024, SectorSize, 256, 0xFFFF));

        long total = regions.Sum(r => (long)r.Length), done = 0;
        foreach (var (offset, length) in regions)
        {
            loader.WriteCompressed(image.AsSpan(offset, length).ToArray(), (uint)offset, written =>
                progress.Report(new("Writing Knob OS", (done + written) / (double)total)), cancel);
            done += length;
        }
        progress.Report(new("Checking what was written", 1));
        foreach (var (offset, length) in regions)
        {
            string expected = Convert.ToHexString(MD5.HashData(image.AsSpan(offset, length))).ToLowerInvariant();
            string actual = loader.FlashMd5((uint)offset, (uint)length);
            if (actual != expected)
                throw new IOException("What was written doesn't match Knob OS. Try again; the knob stays in its loader until it's installed.");
        }
        // Start normally next time (a knob that put itself into the loader sets this), then reset it.
        try { loader.WriteRegister(Option1, 0, ForceDownloadBoot); } catch (IOException) { }
        loader.Reset();
    }

    /// <summary>The parts of the image to write: all of it except the nvs partition(s) in its partition table.</summary>
    internal static List<(int Offset, int Length)> Regions(byte[] image)
    {
        const int TableAt = 0x8000;
        if (image.Length < TableAt + 32 || image[0] != 0xE9)
            throw new InvalidDataException("This isn't a Knob OS image.");
        var skip = new List<(int Start, int End)>();
        for (int at = TableAt; at + 32 <= Math.Min(image.Length, TableAt + 0xC00); at += 32)
        {
            if (image[at] != 0xAA || image[at + 1] != 0x50) break;  // the end, or the table's MD5 entry
            byte type = image[at + 2], subtype = image[at + 3];
            int start = BitConverter.ToInt32(image, at + 4), size = BitConverter.ToInt32(image, at + 8);
            if (type == 1 && subtype == 2) skip.Add((start, start + size));  // data, nvs
        }
        var regions = new List<(int, int)>();
        int from = 0;
        foreach (var (start, end) in skip.OrderBy(s => s.Start))
        {
            if (start >= image.Length) break;
            if (start > from) regions.Add((from, start - from));
            from = Math.Max(from, end);
        }
        if (from < image.Length) regions.Add((from, image.Length - from));
        return regions;
    }

    /// <summary>Restarts the ESP32-S3 into its ROM loader and returns the loader's port.</summary>
    private static string ResetIntoLoader(string portName)
    {
        string? device = UsbConnection.KnobPortDevices()
            .FirstOrDefault(p => p.Port.Equals(portName, StringComparison.OrdinalIgnoreCase)).Device;
        if (device == null || IsSerialJtag(device))
        {
            ResetSerialJtag(portName);
            return portName;
        }
        return RestartFromKnobOs(portName);
    }

    /// <summary>The ESP32-S3's USB Serial/JTAG unit (a knob in its loader, without Knob OS, or with Knob OS before 1.0.2),
    /// by its USB device (e.g. VID_303A&amp;PID_1001&amp;MI_00).</summary>
    private static bool IsSerialJtag(string device) => device.Contains("&PID_1001", StringComparison.OrdinalIgnoreCase);

    private static HashSet<string> SerialJtagPorts() =>
        UsbConnection.KnobPortDevices().Where(p => IsSerialJtag(p.Device)).Select(p => p.Port).ToHashSet(StringComparer.OrdinalIgnoreCase);

    /// <summary>Knob OS's own USB serial port: opened at 1200 baud, the Arduino core restarts the chip into the loader
    /// on the USB Serial/JTAG unit. That's a port that wasn't there before.</summary>
    private static string RestartFromKnobOs(string portName)
    {
        var before = SerialJtagPorts();
        var port = OpenPort(portName, TimeSpan.FromSeconds(5));
        try
        {
            port.BaudRate = 1200;
            Thread.Sleep(200);
        }
        catch (IOException) { }  // the knob already left USB
        finally { Close(port); }
        var until = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < until)
        {
            if (SerialJtagPorts().FirstOrDefault(p => !before.Contains(p)) is { } loader)
            {
                Thread.Sleep(300);  // Windows opens it a moment after it's listed
                return loader;
            }
            Thread.Sleep(100);
        }
        throw new IOException("The knob didn't come back in its loader. Hold its boot button while you plug it in, then install again.");
    }

    /// <summary>Resets the ESP32-S3 into its ROM loader through the USB Serial/JTAG unit: DTR holds the boot pin low
    /// while RTS resets the chip. The knob then leaves USB for a moment and comes back as the loader.</summary>
    private static void ResetSerialJtag(string portName)
    {
        var port = OpenPort(portName, TimeSpan.FromSeconds(5));
        try
        {
            port.DtrEnable = false; port.RtsEnable = false;
            Thread.Sleep(100);
            port.DtrEnable = true; port.RtsEnable = false;
            Thread.Sleep(100);
            // Into reset through (1, 1) rather than (0, 0); Windows sets the lines one at a time.
            port.RtsEnable = true; port.DtrEnable = false;
            port.RtsEnable = true;
            Thread.Sleep(100);
            port.DtrEnable = false; port.RtsEnable = false;
        }
        catch (IOException) { }  // the knob already left USB
        finally { Close(port); }
        WaitForReturn(portName);
    }

    /// <summary>After a reset: waits for the knob to leave USB (if it hasn't yet) and come back.</summary>
    private static void WaitForReturn(string portName)
    {
        bool There() => SerialPort.GetPortNames().Contains(portName, StringComparer.OrdinalIgnoreCase);
        var until = DateTime.UtcNow + TimeSpan.FromSeconds(2);
        while (There() && DateTime.UtcNow < until) Thread.Sleep(50);
        until = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (!There() && DateTime.UtcNow < until) Thread.Sleep(100);
        Thread.Sleep(300);  // Windows opens it a moment after it's listed
    }

    /// <summary>Closes a port whose device may have gone.</summary>
    private static void Close(SerialPort port)
    {
        try { port.Dispose(); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ObjectDisposedException) { }
    }

    /// <summary>Opens the port, retrying while it's busy or not back yet (Knob Hub letting go of it, a knob coming
    /// back on USB after a reset).</summary>
    private static SerialPort OpenPort(string portName, TimeSpan within)
    {
        var until = DateTime.UtcNow + within;
        while (true)
        {
            // A write to a knob that stopped answering gives up rather than hanging the install (Cancel can't stop it).
            var port = new SerialPort(portName, 115200) { DtrEnable = false, RtsEnable = false, Handshake = Handshake.None, WriteTimeout = 5000 };
            try
            {
                port.Open();
                return port;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException)
            {
                port.Dispose();
                if (DateTime.UtcNow > until)
                    throw new IOException($"Couldn't open {portName}: {ex.Message}");
                Thread.Sleep(200);
            }
        }
    }

    private static byte[] Words(params uint[] words)
    {
        var bytes = new byte[words.Length * 4];
        for (int i = 0; i < words.Length; i++) BitConverter.TryWriteBytes(bytes.AsSpan(i * 4), words[i]);
        return bytes;
    }

    /// <summary>The ROM loader on an open port.</summary>
    private sealed class Loader : IDisposable
    {
        private static readonly TimeSpan Short = TimeSpan.FromSeconds(3);

        private readonly SerialPort _port;
        private readonly Stream _stream;
        private readonly CancellationToken _cancel;
        private readonly byte[] _buffer = new byte[4096];
        private int _count, _next;

        private Loader(SerialPort port, CancellationToken cancel)
        {
            _port = port;
            _stream = port.BaseStream;
            _cancel = cancel;
        }

        public static Loader Open(string portName, CancellationToken cancel) =>
            new(OpenPort(portName, TimeSpan.FromSeconds(10)), cancel);

        /// <summary>SYNC until the loader answers (it answers each SYNC several times: the extra answers are dropped).</summary>
        public void Connect()
        {
            var sync = new byte[36];
            sync[0] = 0x07; sync[1] = 0x07; sync[2] = 0x12; sync[3] = 0x20;
            Array.Fill(sync, (byte)0x55, 4, 32);
            for (int attempt = 0; attempt < 10; attempt++)
            {
                try
                {
                    Command(Sync, sync, TimeSpan.FromMilliseconds(300));
                    Drain();
                    return;
                }
                catch (TimeoutException) { }
            }
            throw new IOException("The knob's loader didn't answer. Unplug the knob, plug it in again and retry.");
        }

        public uint ReadRegister(uint address) => Command(ReadReg, Words(address)).Value;

        public void WriteRegister(uint address, uint value, uint mask = 0xFFFFFFFF) =>
            Command(WriteReg, Words(address, value, mask, 0));

        /// <summary>The RTC watchdog and the super watchdog can reset the chip in the middle of a long write when the
        /// loader runs over USB Serial/JTAG: turn the first off and let the second feed itself.</summary>
        public void KeepWatchdogsQuiet()
        {
            try
            {
                WriteRegister(RtcWdtProtect, WdtKey);
                WriteRegister(RtcWdtConfig0, 0);
                WriteRegister(RtcWdtProtect, 0);
                WriteRegister(SwdProtect, SwdKey);
                WriteRegister(SwdConf, ReadRegister(SwdConf) | SwdAutoFeed);
                WriteRegister(SwdProtect, 0);
            }
            catch (IOException) { }  // only a safeguard
        }

        /// <summary>Erases and writes <paramref name="data"/> at <paramref name="offset"/>, sent compressed.</summary>
        public void WriteCompressed(byte[] data, uint offset, Action<long> written, CancellationToken cancel)
        {
            byte[] packed;
            using (var buffer = new MemoryStream())
            {
                using (var zlib = new ZLibStream(buffer, CompressionLevel.SmallestSize, leaveOpen: true)) zlib.Write(data);
                packed = buffer.ToArray();
            }
            int blocks = (packed.Length + BlockSize - 1) / BlockSize;
            // The ROM wants the size to erase rounded up to whole sectors; erasing takes up to about 30 s a megabyte.
            uint erase = (uint)((data.Length + SectorSize - 1) / SectorSize * SectorSize);
            var eraseTime = TimeSpan.FromSeconds(Math.Max(5, 30.0 * erase / (1 << 20)));
            Command(DeflBegin, Words(erase, (uint)blocks, BlockSize, offset, 0), eraseTime);
            for (int i = 0; i < blocks; i++)
            {
                cancel.ThrowIfCancellationRequested();
                int length = Math.Min(BlockSize, packed.Length - i * BlockSize);
                var payload = new byte[16 + length];
                Words((uint)length, (uint)i, 0, 0).CopyTo(payload, 0);
                Array.Copy(packed, i * BlockSize, payload, 16, length);
                byte checksum = 0xEF;
                for (int b = 16; b < payload.Length; b++) checksum ^= payload[b];
                Command(DeflData, payload, TimeSpan.FromSeconds(10), checksum);
                written((long)data.Length * (i + 1) / blocks);
            }
        }

        /// <summary>The MD5 of a stretch of flash, as lowercase hex (the ROM answers with 32 hex characters).</summary>
        public string FlashMd5(uint offset, uint length)
        {
            var (_, data) = Command(SpiFlashMd5, Words(offset, length, 0, 0), TimeSpan.FromSeconds(30));
            return Encoding.ASCII.GetString(data, 0, Math.Min(32, data.Length)).ToLowerInvariant();
        }

        /// <summary>Resets the chip (RTS) with the boot pin free (DTR off), so Knob OS starts.</summary>
        public void Reset()
        {
            try
            {
                _port.DtrEnable = false;
                _port.RtsEnable = true;
                Thread.Sleep(200);
                _port.RtsEnable = false;
            }
            catch (IOException) { }  // it already left USB, restarting
        }

        /// <summary>Sends a command and waits for its answer; throws if the loader says it failed.</summary>
        public (uint Value, byte[] Data) Command(byte op, byte[] data, TimeSpan? timeout = null, uint checksum = 0)
        {
            var packet = new byte[8 + data.Length];
            packet[1] = op;
            BitConverter.TryWriteBytes(packet.AsSpan(2), (ushort)data.Length);
            BitConverter.TryWriteBytes(packet.AsSpan(4), checksum);
            data.CopyTo(packet, 8);
            WriteFrame(packet);

            var until = DateTime.UtcNow + (timeout ?? Short);
            while (true)
            {
                byte[] reply = ReadFrame(until);
                if (reply.Length < 8 || reply[0] != 0x01 || reply[1] != op) continue;  // not this command's answer
                int size = BitConverter.ToUInt16(reply, 2);
                uint value = BitConverter.ToUInt32(reply, 4);
                var body = reply.AsSpan(8, Math.Min(size, reply.Length - 8)).ToArray();
                if (body.Length < 4) throw new IOException("The knob's loader sent an answer that's too short.");
                if (body[^4] != 0)
                    throw new IOException($"The knob's loader refused a step (command 0x{op:X2}, error 0x{body[^3]:X2}).");
                return (value, body[..^4]);
            }
        }

        private void WriteFrame(byte[] packet)
        {
            var frame = new List<byte>(packet.Length + 8) { 0xC0 };
            foreach (byte b in packet)
            {
                if (b == 0xC0) { frame.Add(0xDB); frame.Add(0xDC); }
                else if (b == 0xDB) { frame.Add(0xDB); frame.Add(0xDD); }
                else frame.Add(b);
            }
            frame.Add(0xC0);
            _stream.Write(frame.ToArray());
            _stream.Flush();
        }

        /// <summary>The next SLIP frame, unescaped.</summary>
        private byte[] ReadFrame(DateTime until)
        {
            var frame = new List<byte>();
            bool inFrame = false, escaped = false;
            while (true)
            {
                int b = ReadByte(until);
                if (!inFrame)
                {
                    inFrame = b == 0xC0;
                    continue;
                }
                if (b == 0xC0)
                {
                    if (frame.Count == 0) continue;  // two 0xC0s in a row: the next frame starts here
                    return frame.ToArray();
                }
                if (escaped)
                {
                    frame.Add(b == 0xDC ? (byte)0xC0 : b == 0xDD ? (byte)0xDB : (byte)b);
                    escaped = false;
                }
                else if (b == 0xDB) escaped = true;
                else frame.Add((byte)b);
            }
        }

        private int ReadByte(DateTime until)
        {
            while (_next >= _count)
            {
                _cancel.ThrowIfCancellationRequested();
                var left = until - DateTime.UtcNow;
                if (left <= TimeSpan.Zero) throw new TimeoutException("The knob's loader didn't answer in time.");
                _port.ReadTimeout = (int)Math.Clamp(left.TotalMilliseconds, 1, 500);
                try { _count = _stream.Read(_buffer, 0, _buffer.Length); }
                catch (TimeoutException) { _count = 0; }
                _next = 0;
            }
            return _buffer[_next++];
        }

        /// <summary>Drops whatever arrives in the next moment (the extra answers to SYNC).</summary>
        private void Drain()
        {
            var until = DateTime.UtcNow + TimeSpan.FromMilliseconds(200);
            try { while (true) ReadByte(until); }
            catch (TimeoutException) { }
            _count = _next = 0;
        }

        public void Dispose() => Close(_port);
    }
}
