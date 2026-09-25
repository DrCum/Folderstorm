#!/usr/bin/env bash
# Source this from the viewer repo so the session keeps PATH and AUTOBUILD_VARIABLES_FILE:
#   source scripts/bootstrap-autobuild.sh
#
# Creates .venv, installs requirements.txt, puts that venv's autobuild on PATH,
# and exports AUTOBUILD_VARIABLES_FILE to fs-build-variables/variables.

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
