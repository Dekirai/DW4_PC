#!/bin/sh
# DW4_PC's Gradle entry point for a POSIX shell (Git Bash on Windows, Linux),
# the same as gradlew-dw4.cmd (docs/ANDROID.md, "Gradle user home"): runs
# gradlew for this project in a Gradle user home of DW4_PC's own,
# ~/.gradle-dw4 (%USERPROFILE%\.gradle-dw4 on Windows; or
# DW4_GRADLE_USER_HOME), never in the shared ~/.gradle, and copies
# gradle/cache-retention.init.gradle into that home's init.d before every run.
# Arguments go to gradlew; the project directory is always android/:
#   sh android/gradlew-dw4.sh --offline --no-daemon assembleDebug
set -e
here=$(cd "$(dirname "$0")" && pwd)
base=$HOME
if command -v cygpath >/dev/null 2>&1 && [ -n "$USERPROFILE" ]; then
    base=$(cygpath -u "$USERPROFILE")
fi
home=${DW4_GRADLE_USER_HOME:-$base/.gradle-dw4}
mkdir -p "$home/init.d"
cmp -s "$here/gradle/cache-retention.init.gradle" "$home/init.d/cache-retention.init.gradle" ||
    cp "$here/gradle/cache-retention.init.gradle" "$home/init.d/cache-retention.init.gradle"
if command -v cygpath >/dev/null 2>&1; then
    # Java on Windows needs a Windows path; Git Bash's /c/... is not one.
    home=$(cygpath -m "$home")
    if [ -z "$JAVA_HOME" ] && [ -x "/c/Program Files/Java/jdk-17/bin/java.exe" ]; then
        JAVA_HOME="/c/Program Files/Java/jdk-17"
        export JAVA_HOME
    fi
fi
GRADLE_USER_HOME=$home
export GRADLE_USER_HOME
exec sh "$here/gradlew" -p "$here" "$@"
