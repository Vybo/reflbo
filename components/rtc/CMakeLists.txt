# PCF85063A RTC (spec §7). pcf85063_regs.c is pure C and also built on the host.
idf_component_register(SRCS "pcf85063_regs.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util)
