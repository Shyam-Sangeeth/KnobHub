using System.Windows.Threading;
using KnobHub.Core;
using KnobHub.Sdk;

namespace KnobHub.Apps;

/// <summary>An app's way to the knob. Apps may call it from any thread; the work happens on the UI thread.</summary>
internal sealed class AppHost(AppEntry entry, KnobSession session, Dispatcher ui) : IKnobHost
{
    private volatile bool _isOpen;

    public bool IsOpen => _isOpen;

    internal void SetOpen(bool open) => _isOpen = open;

    public void Show(string screen, KnobScreen content) => Run(() => session.ShowScreen(entry, screen, content));

    public void Update(string screen, object changes) => Run(() => session.UpdateScreen(entry, screen, changes));

    public void Close() => Run(() => session.CloseApp(entry));

    public void Toast(string text) => Run(() => session.Toast(text));

    public void Log(string message) => Run(() => session.Log($"{entry.Manifest.Name}: {message}"));

    public int Setting(string id) => AppManager.GetSetting(entry, id);

    public int ScreenSize => session.ScreenSize;

    private void Run(Action action)
    {
        if (ui.CheckAccess()) action();
        else ui.BeginInvoke(action);
    }
}
