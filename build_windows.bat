@echo off
REM Compile ONIRIQUE BROADCAST (Release). A lancer depuis ce dossier.
cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :err
cmake --build build --config Release || goto :err
echo.
echo OK : build\OniriqueBroadcast_artefacts\Release\ONIRIQUE BROADCAST.exe
goto :eof
:err
echo.
echo ERREUR pendant la compilation, voir les messages ci-dessus.
exit /b 1
