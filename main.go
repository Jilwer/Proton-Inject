package main

import (
	"fmt"
	"os"
	"slices"

	"github.com/proton-inject-go/cli"
	"github.com/proton-inject-go/gui"
)

var version = "0.1.0"

func fatalf(format string, args ...interface{}) {
	fmt.Fprintf(os.Stderr, "error: "+format+"\n", args...)
	os.Exit(1)
}

func main() {
	if len(os.Args) == 1 || slices.Contains(os.Args[1:], "--gui") {
		gui.Version = version
		gui.Run()
		return
	}

	if err := cli.Run(); err != nil {
		fatalf("%v", err)
	}
}
