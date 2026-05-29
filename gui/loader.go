package gui

import (
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"
	"github.com/Jilwer/Proton-Inject/utils"
)

func (s *appState) rebuildLoaderModsView() {
	s.loaderModsBox.Objects = nil
	if len(s.loaderMods) == 0 {
		s.loaderModsBox.Add(widget.NewLabel("(no DLL files found)"))
	} else {
		for _, mod := range s.loaderMods {
			s.loaderModsBox.Add(widget.NewLabel(mod))
		}
	}
	s.loaderModsBox.Refresh()
	if s.loaderModsScroll != nil {
		s.loaderModsScroll.Refresh()
	}
}

func (s *appState) buildLoaderTab() fyne.CanvasObject {
	s.loaderPathLabel = widget.NewLabel("Set AppID in the Inject tab, then click Refresh.")
	s.loaderPathLabel.Wrapping = fyne.TextWrapWord

	s.loaderModsBox = container.NewVBox()
	s.loaderModsScroll = container.NewScroll(s.loaderModsBox)
	s.rebuildLoaderModsView()

	openBtn := widget.NewButton("Open mods directory", func() {
		appID := strings.TrimSpace(s.appIDEntry.Text)
		if appID == "" {
			s.setStatus("Set AppID in the Inject tab first")
			return
		}
		modsDir := utils.ModsDirForAppID(appID)
		if modsDir == "" {
			s.setStatus("No mods directory found yet. Inject the loader once (Inject tab, use embedded loader), then click Refresh.")
			return
		}
		if err := exec.Command("xdg-open", modsDir).Start(); err != nil {
			s.setStatus("Failed to open: " + err.Error())
			return
		}
		s.setStatus("Opened " + modsDir)
		s.refreshLoaderMods()
	})

	refreshBtn := widget.NewButton("Refresh", func() { s.refreshLoaderMods() })

	pathCardDesc := widget.NewLabel("Scanned from all Steam libraries for this AppID. If none is found, inject the loader once so it creates the folder.")
	pathCardDesc.Wrapping = fyne.TextWrapWord
	pathCard := widget.NewCard("Mods directory", "",
		container.NewPadded(container.NewVBox(pathCardDesc, s.loaderPathLabel, container.NewHBox(openBtn, refreshBtn))))
	modsCard := widget.NewCard("DLL files in mods directory", "Mods the loader will load (or has loaded) from the folder above.",
		container.NewPadded(container.NewMax(s.loaderModsScroll)))

	return container.NewBorder(
		container.NewPadded(pathCard),
		nil, nil, nil,
		container.NewPadded(container.NewMax(modsCard)),
	)
}

func (s *appState) refreshLoaderMods() {
	appID := strings.TrimSpace(s.appIDEntry.Text)
	if appID == "" {
		s.loaderPathLabel.SetText("Set AppID in the Inject tab, then click Refresh.")
		s.loaderMods = nil
		s.rebuildLoaderModsView()
		return
	}
	modsDir := utils.ModsDirForAppID(appID)
	if modsDir == "" {
		s.loaderPathLabel.SetText("No proton-inject-mods folder found for this AppID.\n\nInject the loader once (Inject tab: use embedded loader, then run the game and inject). The loader creates the folder. Then click Refresh.")
		s.loaderMods = nil
	} else {
		s.loaderPathLabel.SetText(modsDir)
		s.loaderMods = nil
		entries, err := os.ReadDir(modsDir)
		if err == nil {
			for _, e := range entries {
				if !e.IsDir() && strings.EqualFold(filepath.Ext(e.Name()), ".dll") {
					s.loaderMods = append(s.loaderMods, e.Name())
				}
			}
			sort.Strings(s.loaderMods)
		}
	}
	s.rebuildLoaderModsView()
}
