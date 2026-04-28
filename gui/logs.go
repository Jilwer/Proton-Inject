package gui

import (
	"strings"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"
)

func (s *appState) appendLog(msg string) {
	s.logsMu.Lock()
	timestamp := time.Now().Format("15:04:05")
	formattedMsg := "[" + timestamp + "] " + msg
	s.logs = append(s.logs, formattedMsg)
	if len(s.logs) > 1000 {
		s.logs = s.logs[len(s.logs)-1000:]
	}
	s.logsMu.Unlock()
}

func (s *appState) startLogPoller() {
	go func() {
		for {
			time.Sleep(500 * time.Millisecond)
			s.logsMu.Lock()
			n := len(s.logs)
			if n != s.logLen {
				s.logLen = n
				text := strings.Join(s.logs, "\n")
				s.logsMu.Unlock()
				fyne.Do(func() {
					s.logText.SetText(text)
					s.logText.CursorRow = n - 1
				})
			} else {
				s.logsMu.Unlock()
			}
		}
	}()
}

func (s *appState) buildLogsTab() fyne.CanvasObject {
	clearBtn := widget.NewButton("Clear", func() {
		s.logsMu.Lock()
		s.logs = nil
		s.logLen = 0
		s.logsMu.Unlock()
		s.logText.SetText("")
	})

	toolbar := container.NewHBox(clearBtn)
	return container.NewBorder(container.NewPadded(toolbar), nil, nil, nil, container.NewPadded(s.logText))
}
