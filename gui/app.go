package gui

import (
	"sync"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
	"github.com/Jilwer/Proton-Inject/utils"
)

type appState struct {
	window fyne.Window

	appIDEntry    *widget.Entry
	exeEntry      *widget.Entry
	dllEntry      *widget.Entry
	useLoader     *widget.Check
	profileSelect *widget.Select
	injectButton  *widget.Button

	newProfileName *widget.Entry
	newAppID       *widget.Entry
	newExe         *widget.Entry
	newDll         *widget.Entry
	profileList    *widget.List

	logText     *widget.Entry
	statusLabel *widget.Label

	loaderMods      []string
	loaderList      *widget.List
	loaderPathLabel *widget.Label

	profiles          []string
	selectedProfile   string
	logs              []string
	logsMu            sync.Mutex
	logLen            int
	isInjecting       bool
	syncDllInputState func()
}

func newApp(w fyne.Window) *appState {
	s := &appState{
		window:      w,
		statusLabel: widget.NewLabel("Ready"),
	}

	s.appIDEntry = widget.NewEntry()
	s.appIDEntry.SetPlaceHolder("e.g. 123456")
	s.exeEntry = widget.NewEntry()
	s.exeEntry.SetPlaceHolder("e.g. MyGame.exe")
	s.dllEntry = widget.NewEntry()
	s.dllEntry.SetPlaceHolder("e.g. ~/mods/hook.dll")
	s.useLoader = widget.NewCheck("Use embedded loader", nil)
	s.useLoader.SetChecked(true)
	s.profileSelect = widget.NewSelect([]string{"None"}, nil)
	s.profileSelect.SetSelected("None")

	s.newProfileName = widget.NewEntry()
	s.newProfileName.SetPlaceHolder("e.g. MyGame")
	s.newAppID = widget.NewEntry()
	s.newAppID.SetPlaceHolder("e.g. 123456")
	s.newExe = widget.NewEntry()
	s.newExe.SetPlaceHolder("e.g. MyGame.exe")
	s.newDll = widget.NewEntry()
	s.newDll.SetPlaceHolder("Leave empty for embedded loader")

	s.logText = widget.NewMultiLineEntry()
	s.logText.Disable()

	utils.LogFunc = s.appendLog
	s.refreshProfiles()
	return s
}

func (s *appState) buildUI() fyne.CanvasObject {
	injectTab := s.buildInjectTab()
	profilesTab := s.buildProfilesTab()
	loaderTab := s.buildLoaderTab()
	logsTab := s.buildLogsTab()

	profilesItem := container.NewTabItemWithIcon("Profiles", theme.AccountIcon(), profilesTab)
	loaderItem := container.NewTabItemWithIcon("Loader", theme.FolderOpenIcon(), loaderTab)
	tabs := container.NewAppTabs(
		container.NewTabItemWithIcon("Inject", theme.LoginIcon(), injectTab),
		profilesItem,
		loaderItem,
		container.NewTabItemWithIcon("Logs", theme.DocumentIcon(), logsTab),
	)
	tabs.OnSelected = func(t *container.TabItem) {
		if t == profilesItem {
			s.refreshProfiles()
		}
		if t == loaderItem {
			s.refreshLoaderMods()
		}
	}

	s.profileSelect.OnChanged = func(sel string) {
		if sel == "None" {
			s.selectedProfile = ""
			return
		}
		s.loadProfile(sel)
	}

	s.startLogPoller()

	s.statusLabel.Wrapping = fyne.TextWrapWord
	return container.NewBorder(nil, container.NewPadded(s.statusLabel), nil, nil, container.NewPadded(tabs))
}
