# SPDX-License-Identifier: GPL-3.0-or-later
# Bash has a help builtin. Keep help COMMAND while exposing the mnOS guide.
help() {
    case "${1-}" in
        ''|-cn|--help) command mnos-help "$@" ;;
        *) if [ -n "${BASH_VERSION-}" ] && builtin help "$1" >/dev/null 2>&1; then builtin help "$@";
           else command mnos-help "$@"; fi ;;
    esac
}
