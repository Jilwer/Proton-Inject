package gui

import (
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	xtheme "fyne.io/x/fyne/theme"
)

var Version = "0.1.0"

func Run() {
	a := app.New()
	a.Settings().SetTheme(xtheme.AdwaitaTheme())
	w := a.NewWindow("Proton-Inject")
	w.Resize(fyne.NewSize(640, 600))
	w.CenterOnScreen()

	s := newApp(w)
	w.SetContent(s.buildUI())
	w.ShowAndRun()
}
