package gui

import "github.com/ncruces/zenity"

func pickFile(title string, filters ...zenity.FileFilter) string {
	path, err := zenity.SelectFile(
		zenity.Title(title),
		zenity.FileFilters(filters),
	)
	if err != nil {
		return ""
	}
	return path
}
