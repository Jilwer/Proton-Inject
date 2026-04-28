package gui

import "fmt"

func (s *appState) setStatus(msg string) {
	s.statusLabel.SetText(msg)
}

func (s *appState) setError(err error) {
	s.setStatus(fmt.Sprintf("Error: %v", err))
}

func (s *appState) setErrorf(format string, args ...interface{}) {
	s.setStatus(fmt.Sprintf("Error: "+format, args...))
}
