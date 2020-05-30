@call :GET_CURRENT_DIR
@cd %THIS_DIR%
komodod.exe -ac_name=PIRATE -ac_supply=0 -ac_reward=25600000000 -ac_halving=77777 -ac_private=1 -addnode=178.63.77.56 %1 %2 %3 %4 %5 %6 %7 %8 %9
@goto :EOF

:GET_CURRENT_DIR
@pushd %~dp0
@set THIS_DIR=%CD%
@popd
@goto :EOF
