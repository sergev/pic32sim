Here you can find an example of boot code for PIC32 MX7 and MZ
microcontrollers, based on Boot-CPS application note from Imagination.

See: http://www.imgtec.com/login/?doc=MD00901

You can run the same code in three different configurations of the processor:

    run-mx7.sh              - MX7 processor
    run-mz-no-cache.sh      - MZ processor without cache
    run-mz-with-cache.sh    - MZ processor with cache present

The scripts will produce appropriate trace files:

    mx7.trace
    mz-no-cache.trace
    mz-with-cache.trace
