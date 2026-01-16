SUBDIVISION = 1200

TRIGGER_ADDRESS = 19
TRIGGER_SET_VALUE = 12

DI_ADDRESS_START_VALUE = 20
DI_SET_START_VALUE = 14

PATH_00_ADDRESS = 80
NUM_PATH = 15
ADDRESS_GAP = 6

PATH_END_ADDRESS = PATH_00_ADDRESS + NUM_PATH * ADDRESS_GAP
PATH_ADDRESS = range(PATH_00_ADDRESS, PATH_END_ADDRESS + 1, ADDRESS_GAP)


def integer_to_hex_string(num):
    hex_str = f'{num:02X}'
    if len(hex_str) % 2 == 1:

        hex_str_high = '0' + hex_str[0] + ' '
        hex_str_low = ''

        for i in range(int(len(hex_str) / 2)):
            hex_str_low = hex_str_low + hex_str[i + 1:i * 2 + 3][i:i + 2]
            if i + 1 != int(len(hex_str) / 2):
                hex_str_low += ' '

        hex_str_formatted = hex_str_high + hex_str_low
    else:
        hex_str_formatted = ''
        for i in range(int(len(hex_str) / 2)):

            hex_str_formatted = hex_str_formatted + hex_str[i:i * 2 + 2][i:i + 2]

            if i + 1 != int(len(hex_str) / 2):
                hex_str_formatted += ' '

    return hex_str_formatted


def calc_crc(string):
    data = bytearray.fromhex(string)
    crc_code = 0xFFFF
    for pos in data:
        crc_code ^= pos
        for i in range(8):
            if (crc_code & 1) != 0:
                crc_code >>= 1
                crc_code ^= 0xA001
            else:
                crc_code >>= 1
    hex(((crc_code & 0xff) << 8) + (crc_code >> 8))
    crc_0 = crc_code & 0xff
    crc_1 = crc_code >> 8
    str_crc_0 = '{:02x}'.format(crc_0).upper()
    str_crc_1 = '{:02x}'.format(crc_1).upper()
    code_w = ' ' + str_crc_0 + ' ' + str_crc_1
    statement = string + code_w
    return statement


def position_move_to(num):
    pre = "01 06 00 10 00 "

    DI0_15 = ['00000100', '00001100', '00010100', '00011100', '00100100', '00101100', '00110100', '00111100',
              '01000100', '01001100', '01010100', '01011100', '01100100',
              '01101100', '01110100', '01111100']

    binary_str = DI0_15[num]
    binary_str_no_spaces = binary_str.replace(' ', '')
    integer_val = int(binary_str_no_spaces, 2)
    hex_str_formatted = integer_to_hex_string(integer_val)
    command = calc_crc(pre + hex_str_formatted)

    return command


def position_setting(position, position_0_address, subdivision, speed, degree, clockwise):
    combined_command = ["", ""]
    pre_set_hex = "01 10 "
    pre_speed_hex = "01 06 "

    pos = position * 6 + position_0_address
    print(integer_to_hex_string(pos))

    position_hex = "00 " + integer_to_hex_string(pos)
    position_speed_hex = "00 " + integer_to_hex_string(pos + 2)

    speed_hex = integer_to_hex_string(speed)
    if len(speed_hex) == 2:
        speed_hex = "00 " + speed_hex

    pulse = int(degree * subdivision / 360)
    max_pulse = int("100000000", 16)

    # print(pulse)

    if clockwise:
        pulse_hex = integer_to_hex_string(max_pulse + pulse)[3:]
    else:
        pulse_hex = integer_to_hex_string(max_pulse - pulse)

    combined_command[0] = calc_crc(pre_set_hex + position_hex + " " + "00 02 04 " + pulse_hex)
    combined_command[1] = calc_crc(pre_speed_hex + position_speed_hex + " " + speed_hex)

    return combined_command


def path_selection_switch_pre(trigger_di, trigger_sp, di_address_start, sp_address_start):
    pre = "01 06 00 "

    di0_to_3_address = range(di_address_start, di_address_start + 4, 1)
    sp0_to_3_address = range(sp_address_start, sp_address_start + 4, 1)

    di0_to_3_address_hex = ["" for _ in range(len(di0_to_3_address))]
    sp0_to_3_address_hex = ["" for _ in range(len(sp0_to_3_address))]

    num = len(di0_to_3_address)

    for j in range(num):
        di0_to_3_address_hex[j] = integer_to_hex_string(di0_to_3_address[j])
        sp0_to_3_address_hex[j] = integer_to_hex_string(sp0_to_3_address[j])

    command = ["" for _ in range(num + 1)]
    command[0] = calc_crc(pre + integer_to_hex_string(trigger_di) + " 00 " + integer_to_hex_string(trigger_sp))
    for j in range(num):
        command[1 + j] = calc_crc(pre + di0_to_3_address_hex[j] + " 00 " + sp0_to_3_address_hex[j])

    return command


def position_clear():
    return "01 06 00 10 00 00 88 0F"


def set_origin():
    return "01 06 00 4F 04 00 BA DD"


if __name__ == '__main__':

    print(calc_crc("01 06 00 36 00 16"))
    preset = path_selection_switch_pre(TRIGGER_ADDRESS, TRIGGER_SET_VALUE, DI_ADDRESS_START_VALUE, DI_SET_START_VALUE)

    # for i in range(len(preset)):
    #     print(preset[i])
    #
    # position_set = position_setting(0, PATH_ADDRESS[0], SUBDIVISION, 100, 360, 0)
    # print(position_set[0])
    # print(position_set[1])
    #
    # move = position_move_to(12)
    # print(move)
    #
    # clear = position_clear()
    # print(clear)
    # position_set = [""] * 12
    #
    # for n in range(12):
    #     position_set[n] = position_setting(n, PATH_ADDRESS[0], SUBDIVISION, 100, 30 * n, 1)
    #     print(position_set[n][0])
    #     print(position_set[n][1])
    #
    #     print()
    # move = position_move_to(2)
    # print(move)