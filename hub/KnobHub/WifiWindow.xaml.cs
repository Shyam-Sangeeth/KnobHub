using System.Windows;
using System.Windows.Controls;
using KnobHub.Core;

namespace KnobHub;

/// <summary>Wi-Fi, opened from Settings > Connection as a small modal window: the knob looks for networks, and the one
/// chosen is joined there and then (the password goes only to the knob). The knob's own screen says how it went too.
/// "Use a phone" starts pairing mode instead.</summary>
public partial class WifiWindow : Window
{
    private readonly KnobSession _session;
    private string? _asked;     // the network the knob was asked to join
    private bool _seenJoining;  // the knob has reported joining it (until then, older results don't count)

    internal WifiWindow(KnobSession session, Window owner)
    {
        InitializeComponent();
        _session = session;
        Owner = owner;
        session.WifiNetworksFound += OnNetworks;
        session.StateChanged += ShowState;
        session.WifiScreenLeft += Close;  // the knob was used meanwhile
        Closed += (_, _) =>
        {
            session.WifiNetworksFound -= OnNetworks;
            session.StateChanged -= ShowState;
            session.WifiScreenLeft -= Close;
            session.WifiDone();
        };
        Loaded += (_, _) =>
        {
            Scan();
            ShowState();
        };
    }

    private void Scan()
    {
        Networks.ItemsSource = null;
        ListNote.Text = "Looking for networks…";
        _session.WifiScan();
    }

    private void OnNetworks(IReadOnlyList<WifiNetworkInfo> networks)
    {
        Networks.ItemsSource = networks;
        ListNote.Text = networks.Count == 0 ? "The knob can't see any networks. Type the name below, or look again." : "";
        // The network it's on, chosen already.
        if (_session.WifiState == "connected" && networks.FirstOrDefault(n => n.Ssid == _session.WifiNetwork) is { } current)
            Networks.SelectedItem = current;
    }

    private void Refresh_Click(object sender, RoutedEventArgs e) => Scan();

    private void Networks_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Networks.SelectedItem is not WifiNetworkInfo network) return;
        NetworkBox.Text = network.Ssid;
        if (network.Open) Status.Text = "";
        if (IsLoaded && Networks.IsKeyboardFocusWithin) (ShowPassword.IsChecked == true ? (Control)PasswordText : PasswordBox).Focus();
    }

    private void ShowPassword_Changed(object sender, RoutedEventArgs e)
    {
        bool show = ShowPassword.IsChecked == true;
        if (show) PasswordText.Text = PasswordBox.Password;
        else PasswordBox.Password = PasswordText.Text;
        PasswordText.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        PasswordBox.Visibility = show ? Visibility.Collapsed : Visibility.Visible;
    }

    private string Password => ShowPassword.IsChecked == true ? PasswordText.Text : PasswordBox.Password;

    private void Connect_Click(object sender, RoutedEventArgs e)
    {
        string ssid = NetworkBox.Text.Trim();
        if (ssid.Length == 0)
        {
            Status.Text = "Choose a network, or type its name.";
            return;
        }
        bool open = (Networks.ItemsSource as IEnumerable<WifiNetworkInfo>)?.FirstOrDefault(n => n.Ssid == ssid)?.Open == true;
        string password = open ? "" : Password;
        if (!open && password.Length is > 0 and < 8)
        {
            Status.Text = "Wi-Fi passwords are at least 8 characters.";
            return;
        }
        _asked = ssid;
        _seenJoining = false;
        _session.WifiJoin(ssid, password);
        Status.Text = $"Joining {ssid}…";
        ConnectButton.IsEnabled = false;
    }

    private void Phone_Click(object sender, RoutedEventArgs e)
    {
        _session.WifiAction("setup");
        Close();
    }

    /// <summary>How the join is going, from the knob's Wi-Fi messages.</summary>
    private void ShowState()
    {
        if (!_session.KnobConnected)
        {
            Status.Text = _asked != null && _seenJoining
                ? $"The knob's link dropped while it joined {_asked}: look at the knob's screen, it says how it went."
                : "The knob isn't connected.";
            ConnectButton.IsEnabled = false;
            return;
        }
        if (_asked == null)
        {
            ConnectButton.IsEnabled = true;
            return;
        }
        if (_session.WifiJoining == _asked)
        {
            _seenJoining = true;
            return;
        }
        if (!_seenJoining) return;
        ConnectButton.IsEnabled = true;
        if (_session.WifiState == "connected" && _session.WifiNetwork == _asked)
            Status.Text = $"Connected to {_asked}. Knob Hub can reach the knob over Wi-Fi when it isn't plugged in.";
        else if (_session.WifiJoinFailed == _asked)
            Status.Text = Problem(_asked, _session.WifiJoinProblem);
        else
            return;
        _asked = null;
    }

    private static string Problem(string ssid, string problem) => problem switch
    {
        "password" => $"{ssid} turned the password down. Check it (Show password helps) and try again.",
        "not_found" => $"The knob can't see {ssid}. It only uses 2.4 GHz networks: check that {ssid} has one and the knob is in range.",
        "security" => $"{ssid} uses security the knob can't use: it needs WPA2 or WPA3.",
        "timeout" => $"{ssid} didn't answer in time. Try again.",
        _ => $"The knob couldn't join {ssid}. Try again.",
    };
}
