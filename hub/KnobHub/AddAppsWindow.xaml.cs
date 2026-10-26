using System.Collections.Specialized;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using KnobHub.Apps;
using KnobHub.Core;
using Microsoft.Win32;

namespace KnobHub;

/// <summary>Add apps, opened from the Apps page as a modal window: the apps released on GitHub (installed or updated
/// from there) and installing a .knobapp file.</summary>
public partial class AddAppsWindow : Window
{
    private readonly MainViewModel _model;
    private readonly AppManager _manager;

    internal AddAppsWindow(MainViewModel model, AppManager manager, Window owner)
    {
        InitializeComponent();
        _model = model;
        _manager = manager;
        DataContext = model;
        Owner = owner;
        SizeToScreen(owner, 0.75);

        // The search filters the list's view; the list itself stays whole.
        _view = CollectionViewSource.GetDefaultView(model.Catalog);
        _view.Filter = item => Matches((CatalogItem)item, SearchBox.Text);
        model.Catalog.CollectionChanged += OnCatalogChanged;
        Closed += (_, _) =>
        {
            model.Catalog.CollectionChanged -= OnCatalogChanged;
            _view.Filter = null;
        };
        Loaded += (_, _) =>
        {
            SearchBox.Focus();
            _ = model.LoadCatalogAsync(once: true);
        };
    }

    private readonly ICollectionView _view;

    /// <summary>Whether the app's name, description, author or id contains every word searched for.</summary>
    private static bool Matches(CatalogItem item, string search)
    {
        var m = item.App.Manifest;
        string text = $"{m.Name} {m.Description} {m.Subtitle} {m.Author} {m.Id}";
        return search.Split(' ', StringSplitOptions.RemoveEmptyEntries)
                     .All(word => text.Contains(word, StringComparison.CurrentCultureIgnoreCase));
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e) => ApplySearch();

    // Esc clears the search first; with nothing searched it closes the window (the Close button is the cancel button).
    private void SearchBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape || SearchBox.Text.Length == 0) return;
        SearchBox.Clear();
        e.Handled = true;
    }

    private void OnCatalogChanged(object? sender, NotifyCollectionChangedEventArgs e) => ApplySearch();

    private void ApplySearch()
    {
        _view.Refresh();
        string search = SearchBox.Text.Trim();
        bool none = search.Length > 0 && _model.Catalog.Count > 0 && _view.IsEmpty;
        NoMatch.Text = $"No apps match \"{search}\".";
        NoMatch.Visibility = none ? Visibility.Visible : Visibility.Collapsed;
    }

    /// <summary>Opens at <paramref name="share"/> of the width and height of the screen the owner is on (without the
    /// taskbar), centred over the owner.</summary>
    private void SizeToScreen(Window owner, double share)
    {
        var screen = System.Windows.Forms.Screen.FromHandle(new WindowInteropHelper(owner).Handle).WorkingArea;
        var dpi = VisualTreeHelper.GetDpi(owner);
        Width = Math.Max(MinWidth, screen.Width * share / dpi.DpiScaleX);
        Height = Math.Max(MinHeight, screen.Height * share / dpi.DpiScaleY);
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    private void Refresh_Click(object sender, RoutedEventArgs e) => _ = _model.LoadCatalogAsync();

    // The repository box takes effect on Enter as well as when it loses focus.
    private void RepoBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) RepoBox.GetBindingExpression(TextBox.TextProperty)?.UpdateSource();
    }

    private void InstallFile_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Title = "Install a Knob Hub app", Filter = "Knob Hub apps (*.knobapp)|*.knobapp" };
        if (dialog.ShowDialog(this) == true) InstallPackage(dialog.FileName, from: null, expectedId: null);
    }

    /// <summary>Downloads an app from the list and installs it (or the update).</summary>
    private async void Install_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is not CatalogItem item || item.Busy) return;
        item.Busy = true;
        string? path = null;
        try
        {
            path = await AppCatalog.DownloadAsync(item.App);
            InstallPackage(path, from: $"GitHub ({_model.AppsRepo})", expectedId: item.App.Id);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"Couldn't download {item.Name}: {ex.Message}", "Install app", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            item.Busy = false;
            try { if (path != null) File.Delete(path); } catch (Exception) { }
        }
    }

    /// <summary>Installs a .knobapp after asking: plugins run with Knob Hub's permissions.</summary>
    private void InstallPackage(string path, string? from, string? expectedId)
    {
        try
        {
            var manifest = AppManager.ReadPackage(path);
            if (expectedId != null && !string.Equals(manifest.Id, expectedId, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException($"The download is \"{manifest.Id}\", not the app the list shows (\"{expectedId}\").");
            string by = string.IsNullOrEmpty(manifest.Author) ? "" : $" by {manifest.Author}";
            string version = string.IsNullOrEmpty(manifest.Version) ? "" : $" {manifest.Version}";
            string source = from == null ? "" : $" from {from}";
            var answer = MessageBox.Show(this,
                $"Install {manifest.Name}{version}{by}{source}?\n\nIt runs on your PC with Knob Hub's permissions. Only install apps you trust.",
                "Install app", MessageBoxButton.YesNo, MessageBoxImage.Question);
            if (answer != MessageBoxResult.Yes) return;
            var entry = _manager.Install(path);
            if (entry.Error != null)
                MessageBox.Show(this, $"{entry.Manifest.Name} was installed but couldn't start:\n\n{entry.Error}", "Install app",
                                MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Couldn't install the app", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
