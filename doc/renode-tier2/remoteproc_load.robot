*** Variables ***
${LINUX_UART}      sysbus.uart1
${LINUX_PROMPT}    \#${SPACE}
${ROOTFS}          %{POUCH_ROOTFS}

*** Keywords ***
Boot Linux And Login
    [Arguments]    ${testerId}=0
    Wait For Line On Uart      Booting Linux on physical CPU    testerId=${testerId}
    Wait For Prompt On Uart    buildroot login:                 testerId=${testerId}    timeout=120
    Write Line To Uart         root                             testerId=${testerId}
    Wait For Prompt On Uart    ${LINUX_PROMPT}                  testerId=${testerId}

Linux Command
    [Arguments]    ${command}    ${timeout}=15
    Write Line To Uart         ${command}
    Wait For Prompt On Uart    ${LINUX_PROMPT}    timeout=${timeout}

*** Test Cases ***
Should Load Pouch OpenAMP Firmware Via Remoteproc
    Execute Command            $rootfs=@${ROOTFS}
    Execute Command            include @scripts/single-node/zynqmp_openamp.resc
    Execute Command            machine SetSerialExecution True
    Create Terminal Tester     ${LINUX_UART}    timeout=300    defaultPauseEmulation=true
    Start Emulation
    Boot Linux And Login
    Linux Command              modprobe zynqmp_r5_remoteproc
    Linux Command              echo rpmsg-echo.out > /sys/class/remoteproc/remoteproc0/firmware
    Linux Command              echo start > /sys/class/remoteproc/remoteproc0/state    timeout=45
    Write Line To Uart         cat /sys/class/remoteproc/remoteproc0/state
    Wait For Line On Uart      running    timeout=15
    Write Line To Uart         dmesg | tail -25
    Wait For Prompt On Uart    ${LINUX_PROMPT}    timeout=15
