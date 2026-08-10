*** Variables ***
${ELF}    %{ZEPHYR_ELF=build/zephyr/zephyr.elf}

*** Test Cases ***
Should Boot Zephyr Hello World On R5
    Execute Command             mach create
    Execute Command             machine LoadPlatformDescription @platforms/cpus/zynqmp.repl
    Execute Command             machine SetSerialExecution True
    Execute Command             using sysbus
    Execute Command             using sysbus.cluster0
    Execute Command             using sysbus.cluster1
    Execute Command             cluster0 ForEach IsHalted true
    Execute Command             cluster1 ForEach IsHalted true
    Execute Command             rpu0 IsHalted false
    Execute Command             sysbus LoadELF @${ELF} cpu=rpu0
    Create Terminal Tester      sysbus.uart1    timeout=30
    Start Emulation
    Wait For Line On Uart       Hello World
