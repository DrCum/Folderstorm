#!/usr/bin/env bash
# Source this from the viewer repo so the session keeps PATH and AUTOBUILD_VARIABLES_FILE:
#   source scripts/bootstrap-autobuild.sh
#
# Creates .venv, installs requirements.txt, puts that venv's autobuild on PATH,
# puts Go on PATH when it is only in a usual install location, and exports
# AUTOBUILD_VARIABLES_FILE to fs-build-variables/variables.

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "Source this script so the session keeps PATH and AUTOBUILD_VARIABLES_FILE:" >&2
  echo "  source scripts/bootstrap-autobuild.sh" >&2
  exit 1
fi

_boot_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_repo_root="$(cd "${_boot_dir}/.." && pwd)"

if [[ ! -f "${_repo_root}/fs-build-variables/variables" ]]; then
  echo "bootstrap-autobuild: missing ${_repo_root}/fs-build-variables/variables" >&2
  return 1
fi

# PATH first, then the same install locations as indra/cmake/GoToolchain.cmake.
# Prepend the bin directory so configure works without a new terminal.
_folderstorm_require_go() {
  local go_exe="" from_path=0 hint major minor ver_line ver_rc hints_nl
  case "$(uname -s)" in
    Darwin)
      hints_nl=$'/usr/local/go/bin\n/opt/homebrew/bin\n/usr/local/bin'
      ;;
    *)
      hints_nl=$'/usr/local/go/bin\n/usr/lib/go/bin'
      ;;
  esac

  if command -v go >/dev/null 2>&1; then
    go_exe="$(command -v go)"
    from_path=1
  else
    while IFS= read -r hint; do
      [[ -z "${hint}" ]] && continue
      if [[ -x "${hint}/go" ]]; then
        go_exe="${hint}/go"
        break
      fi
    done <<< "${hints_nl}"
  fi

  if [[ -z "${go_exe}" ]]; then
    echo "bootstrap-autobuild: Go 1.25 or newer is required to build migrate-settings and fs-mcp." >&2
    echo "Searched PATH and:" >&2
    while IFS= read -r hint; do
      [[ -z "${hint}" ]] && continue
      echo "  ${hint}" >&2
    done <<< "${hints_nl}"
    echo "Install Go 1.25 or newer, or add it to PATH." >&2
    return 1
  fi

  ver_rc=0
  ver_line="$("${go_exe}" version 2>&1)" || ver_rc=$?
  if [[ "${ver_rc}" -ne 0 ]]; then
    echo "bootstrap-autobuild: '${go_exe} version' failed (exit ${ver_rc})." >&2
    echo "${ver_line}" >&2
    echo "Go 1.25 or newer is required to build migrate-settings and fs-mcp." >&2
    return 1
  fi

  if [[ "${ver_line}" =~ go([0-9]+)\.([0-9]+) ]]; then
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
  else
    echo "bootstrap-autobuild: could not parse the Go version from '${ver_line}' (${go_exe})." >&2
    echo "Go 1.25 or newer is required to build migrate-settings and fs-mcp." >&2
    return 1
  fi

  if (( major < 1 || (major == 1 && minor < 25) )); then
    echo "bootstrap-autobuild: found ${ver_line} at ${go_exe}." >&2
    echo "Go 1.25 or newer is required to build migrate-settings and fs-mcp." >&2
    echo "Install Go 1.25 or newer, or add it to PATH." >&2
    return 1
  fi

  if [[ "${from_path}" -eq 0 ]]; then
    PATH="$(dirname "${go_exe}"):${PATH}"
    export PATH
  fi
  echo "go: ${go_exe} (${ver_line})"
}

_folderstorm_require_go || return 1

_py=""
for _cand in python3.10 python3.11 python3.12 python3.13 python3; do
  if command -v "$_cand" >/dev/null 2>&1; then
    _py="$_cand"
    break
  fi
done
if [[ -z "${_py}" ]]; then
  echo "bootstrap-autobuild: python3 was not found." >&2
  return 1
fi

if [[ ! -x "${_repo_root}/.venv/bin/python" ]]; then
  "${_py}" -m venv "${_repo_root}/.venv" || return 1
fi

# shellcheck disable=SC1091
source "${_repo_root}/.venv/bin/activate"
python -m pip install -r "${_repo_root}/requirements.txt" || return 1

if ! command -v autobuild >/dev/null 2>&1; then
  echo "bootstrap-autobuild: autobuild is not on PATH after installing requirements.txt" >&2
  return 1
fi

export AUTOBUILD_VARIABLES_FILE="${_repo_root}/fs-build-variables/variables"
echo "AUTOBUILD_VARIABLES_FILE=${AUTOBUILD_VARIABLES_FILE}"
command -v autobuild

unset _boot_dir _repo_root _py _cand
unset -f _folderstorm_require_go
