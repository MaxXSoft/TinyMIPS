`ifndef TINYMIPS_TEST_INCLUDE_TB_V_
`define TINYMIPS_TEST_INCLUDE_TB_V_

`define TICK  _tb_tick

`define GEN_TICK(clk, rst)                                        \
    integer `TICK;                                                \
    always @(posedge clk) begin                                   \
      if (rst) begin                                              \
        `TICK <= 0;                                               \
      end                                                         \
      else begin                                                  \
        `TICK <= `TICK + 1;                                       \
      end                                                         \
    end

`define DISPLAY(name, val)                                        \
    $display("[%8h] %s = 0x%8h", `TICK, name, val)

`define ASSERT(name, val, tick, expect)                           \
    if ((tick) == `TICK && (val) != (expect)) begin               \
      $display("[%8h] ERROR! Assertion failed at signal '%s', ",  \
               `TICK, name,                                       \
               "expected = 0x%8h, actual = 0x%8h",                \
               expect, val);                                      \
      $finish;                                                    \
    end

`define END_AT_TICK(tick)                                         \
    if (`TICK >= tick) $finish

`endif  // TINYMIPS_TEST_INCLUDE_TB_V_
