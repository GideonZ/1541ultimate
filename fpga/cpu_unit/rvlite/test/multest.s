# Setup: Define the error flag address
.equ ERROR_ADDR, 0xFFFC

.section .text
.globl _start

_start:
    # --- Test 1: basic mul (Lower 32 bits) ---
    # 0x00010000 * 0x00010000 = 0x0100000000 (32nd bit is 1, lower 32 are 0)
    li t0, 0x00010000
    mul t1, t0, t0          # Result should be 0x00000000
    li t2, 0x00000000
    bne t1, t2, fail

    # --- Test 2: mulh (Signed x Signed) ---
    # -2 * 2 = -4. In 64-bit: 0xFFFFFFFFFFFFFFFC
    # Upper 32 bits (mulh) should be 0xFFFFFFFF
    li t0, -2
    li t1, 2
    mulh t2, t0, t1
    li t3, 0xFFFFFFFF
    bne t2, t3, fail

    # --- Test 3: mulhu (Unsigned x Unsigned) ---
    # 0xFFFFFFFF * 2 = 0x1FFFFFFFE
    # Upper 32 bits (mulhu) should be 0x00000001
    li t0, 0xFFFFFFFF
    li t1, 2
    mulhu t2, t0, t1
    li t3, 0x00000001
    bne t2, t3, fail

    # --- Test 4: mulhsu (Signed x Unsigned) ---
    # -1 (0xFFFFFFFF) * 2 (Unsigned)
    # This is treated as (-1) * (2) = -2 (0xFFFFFFFFFFFFFFFE)
    # Upper 32 bits should be 0xFFFFFFFF
    li t0, -1
    li t1, 2
    mulhsu t2, t0, t1
    li t3, 0xFFFFFFFF
    bne t2, t3, fail

pass:
    li t0, 0                # 0 = No Error
    li t1, ERROR_ADDR
    sw t0, 0(t1)
    j end

fail:
    li t0, 1                # 1 = Error Detected
    li t1, ERROR_ADDR
    sw t0, 0(t1)

end:
    ebreak                  # Exit or loop forever
    j end
    