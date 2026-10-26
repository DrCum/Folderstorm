// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows.Controls;

namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
    private void SearchTextures(object sender, TextChangedEventArgs args) => RefreshTextures();
    private void RefreshTextures()
    {
        if (catalog == null || TextureList == null || TextureSearch == null) return;
        var selected = (TextureList.SelectedItem as TextureDefinition)?.Name;
        var query = TextureSearch.Text;
        var textures = catalog.Textures.Values.Where(texture => texture.Name.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                texture.Attributes.GetValueOrDefault("file_name", "").Contains(query, StringComparison.OrdinalIgnoreCase))
            .OrderBy(texture => texture.Name, StringComparer.Ordinal).ToArray();
        TextureList.ItemsSource = textures;
        TextureList.SelectedItem = textures.FirstOrDefault(texture => texture.Name == selected) ?? textures.FirstOrDefault();
        if (textures.Length == 0) { TextureName.Text = "No matching images"; AssetImage.Source = null; TextureSource.Text = ""; }
    }
}
