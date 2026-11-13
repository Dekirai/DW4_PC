@echo off
rem DW4_PC's Gradle entry point (docs/ANDROID.md, "Gradle user home"): runs
rem gradlew.bat for this project in a Gradle user home of DW4_PC's own,
rem %USERPROFILE%\.gradle-dw4 (or DW4_GRADLE_USER_HOME), never in the shared
rem %USERPROFILE%\.gradle: Gradle cleans the user home it runs in and deletes
rem what no build there used for 30 days, so a run in the shared home can
rem delete other projects' caches (and theirs DW4_PC's). Before every run it
rem copies gradle\cache-retention.init.gradle into that home's init.d (365
rem days instead of 30). JAVA_HOME defaults to %ProgramFiles%\Java\jdk-17.
rem Arguments go to gradlew; the project directory is always android\:
rem   android\gradlew-dw4.cmd --offline --no-daemon assembleDebug
setlocal
if not defined DW4_GRADLE_USER_HOME set "DW4_GRADLE_USER_HOME=%USERPROFILE%\.gradle-dw4"
set "GRADLE_USER_HOME=%DW4_GRADLE_USER_HOME%"
if not defined JAVA_HOME if exist "%ProgramFiles%\Java\jdk-17\bin\java.exe" set "JAVA_HOME=%ProgramFiles%\Java\jdk-17"
if not exist "%GRADLE_USER_HOME%\init.d\" mkdir "%GRADLE_USER_HOME%\init.d" || exit /b 1
fc /b "%~dp0gradle\cache-retention.init.gradle" "%GRADLE_USER_HOME%\init.d\cache-retention.init.gradle" >nul 2>&1 || copy /y "%~dp0gradle\cache-retention.init.gradle" "%GRADLE_USER_HOME%\init.d\cache-retention.init.gradle" >nul || exit /b 1
call "%~dp0gradlew.bat" -p "%~dp0." %*
exit /b %errorlevel%
