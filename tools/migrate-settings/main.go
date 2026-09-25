package main

import (
	"bufio"
	"fmt"
	"os"
	"runtime"
	"strings"
)

func main() {
	os.Exit(run(os.Args[1:]))
}

func run(args []string) int {
	opts, code, help := parseArgs(args)
	if help {
		return code
	}
	fullyInteractive := !opts.Yes && !opts.Accounts && !opts.DryRun && !opts.Check && !opts.CheckAccounts
	if fullyInteractive {
		opts.Interactive = true
		if isCharDevice(os.Stdin) {
			opts.Ask = terminalAsk
		} else {
			opts.Ask = guiAsk
		}
	}
	if opts.Platform == "" {
		opts.Platform = detectPlatform(runtime.GOOS)
	}
	result := Run(opts)
	if result.Stdout != "" {
		fmt.Fprint(os.Stdout, result.Stdout)
	}
	if result.Stderr != "" {
		fmt.Fprint(os.Stderr, result.Stderr)
	}
	if fullyInteractive && runtime.GOOS == "windows" && len(os.Args) == 1 && isCharDevice(os.Stdin) {
		fmt.Fprint(os.Stderr, "\nPress Enter to close.")
		bufio.NewReader(os.Stdin).ReadString('\n')
	}
	return result.Code
}

func parseArgs(args []string) (Options, int, bool) {
	var opts Options
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch arg {
		case "-h", "--help":
			fmt.Fprint(os.Stdout, usageText)
			return opts, 0, true
		case "--yes":
			opts.Yes = true
		case "--overwrite":
			opts.Overwrite = true
		case "--accounts":
			opts.Accounts = true
		case "--overwrite-accounts":
			opts.OverwriteAccounts = true
		case "--dry-run":
			opts.DryRun = true
		case "--check":
			opts.Check = true
		case "--check-accounts":
			opts.CheckAccounts = true
		case "--source", "--dest":
			if i+1 >= len(args) {
				fmt.Fprintf(os.Stderr, "%s needs a directory.\n", arg)
				return opts, exitError, true
			}
			i++
			if arg == "--source" {
				opts.Source = args[i]
			} else {
				opts.Dest = args[i]
			}
		default:
			fmt.Fprintf(os.Stderr, "Unknown option %s\n%s", arg, usageText)
			return opts, exitError, true
		}
	}
	if opts.Check && opts.CheckAccounts {
		fmt.Fprintln(os.Stderr, "Use --check or --check-accounts, not both.")
		return opts, exitError, true
	}
	return opts, 0, false
}

const usageText = `migrate-settings copies Firestorm settings into Folderstorm.

It does nothing unless you pass --yes, pass --accounts, or answer the prompts.
Saved passwords, cookies, assistant tokens, caches, and logs are not copied.
Existing Folderstorm settings files are kept unless you pass --overwrite or
confirm that prompt. Per-account folders, including toolbars.xml, are a
separate copy. An existing Folderstorm account folder is kept unless you pass
--overwrite-accounts or confirm that prompt.

  migrate-settings
  migrate-settings --yes
  migrate-settings --yes --overwrite
  migrate-settings --accounts
  migrate-settings --accounts --overwrite-accounts
  migrate-settings --dry-run

--check prints nothing. It exits 0 when settings can be copied without
replacing a file, 2 when there is nothing to copy, and 3 when Folderstorm
already has some of those files. The settings folder existing is not itself
a reason to exit 3.
--check-accounts is the same check for per-account folders.
`

func isCharDevice(file *os.File) bool {
	info, err := file.Stat()
	if err != nil {
		return false
	}
	return info.Mode()&os.ModeCharDevice != 0
}

func terminalAsk(message string) bool {
	fmt.Fprintln(os.Stdout, message)
	fmt.Fprint(os.Stdout, "[y/N] ")
	line, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if err != nil && line == "" {
		return false
	}
	answer := strings.TrimSpace(line)
	return strings.EqualFold(answer, "y") || strings.EqualFold(answer, "yes")
}

func guiAsk(message string) bool {
	yes, available := askGUI("Folderstorm", message)
	if !available {
		fmt.Fprintln(os.Stderr, "Could not open a prompt. Run migrate-settings in a terminal.")
		return false
	}
	return yes
}
