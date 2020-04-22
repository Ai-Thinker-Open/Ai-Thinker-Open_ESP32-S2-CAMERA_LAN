json = require('json')
dump = require('dump')
wifi = require('wifi')

local LV_SYMBOL_LEFT  = "\xef\x81\x93"
local LV_SYMBOL_RIGHT = "\xef\x81\x94"
local LV_SYMBOL_UP    = "\xef\x81\xb7"
local LV_SYMBOL_DOWN  = "\xef\x81\xb8"

function lcd.print(str)
    return lcd.write('MSG', str)
end

lcd.write('BATT', '0')
print(dump.table(sys.info()))
lcd.write('MEM', string.format('%d', math.floor(sys.info().heap / 1024)))
if (sys.info().cpu) then
    lcd.write('CPU', sys.info().cpu)
end
lcd.print('Connecting\nWiFi\n...')
lcd.write('WIFI', '0')
if (not wifi.start_sta('test', '123456789')) then
    print('Connect to AP and log in to http://192.168.1.1 and configure router information')
    lcd.print('Connect to AP and log in to http://192.168.1.1 and configure router information')
    wifi.start_ap('ESP32-S2-HMI', '')
end
httpd.start('ESP32-S2-HMI')
lcd.write('WIFI', '1')
print(dump.table(net.info()))
assert(sys.sntp('ntp1.aliyun.com'))
print(os.date("%Y-%m-%d %H:%M:%S"))
lcd.print('Welcome\n #ff0000 XiongYu#')

local info = {}
local ram_file = ramf.malloc(200 * 1024)
local last_1s = os.time()
local last_30s = os.time() - 20
while (1) do
    if (os.difftime (os.time(), last_1s) >= 1) then
        lcd.write('STATE', os.date("%H:%M:%S"))
        if (net.info().ip.sta == '0.0.0.0') then
            lcd.write('WIFI', '0')
        else
            lcd.write('WIFI', '1')
        end
        local heap = sys.info().heap / 1024
        if (heap > 100) then
            lcd.write('BATT', '0')
        elseif (heap > 50) then
            lcd.write('BATT', '1')
        elseif (heap > 20) then
            lcd.write('BATT', '2')
        elseif (heap > 10) then
            lcd.write('BATT', '3')
        else
            lcd.write('BATT', '4')
        end
        lcd.write('MEM', string.format('%d', math.floor(heap)))
        if (sys.info().cpu) then
            lcd.write('CPU', sys.info().cpu)
        end
        print(dump.table(net.info()))
        print(dump.table(sys.info()))
        last_1s = os.time()
    end
    sys.yield()

    if (os.difftime (os.time(), last_30s) >= 30) then
        info = {}
        local display = '#FF6100 '..os.date("%Y-%m-%d")..'#\n'..'#00BFFF '..net.info().ip.sta..'#\n'..'#FFC0CB '..net.info().mac.sta..'#'
        local shares = web.rest('GET', 'http://hq.sinajs.cn/list=sh688018')
        if (shares) then
            web.file(ram_file, 'http://image.sinajs.cn/newchart/small/nsh688018.png')
            lcd.write('SHARES', ram_file)
            local shares_json = '['..string.match(string.gsub(shares, ',', '\",\"'), '=(.*);')..']'
            local shares_t = json.decode(shares_json)
            local price_cur = tonumber(shares_t[4])
            local price_last = tonumber(shares_t[3])
            local price_diff = price_cur - price_last
            local price_percentage = price_diff / price_last
            local price_color = (price_percentage > 0) and 'FF0000' or '00FF00'
            local price_symbol = (price_percentage > 0) and LV_SYMBOL_UP or LV_SYMBOL_DOWN
            display = display..'\n#FF0000 '..LV_SYMBOL_LEFT..'ESPRESSIF'..LV_SYMBOL_RIGHT..'#\n'..string.format('#%s %.2f %s#\n#%s %+.2f %+.2f%%#', price_color, price_cur, price_symbol, price_color, price_diff, price_percentage * 100)
            info.shares = shares_t
        end
        print(display)
        lcd.print(display)
        info.clock = os.clock()
        info.date = os.date("%Y-%m-%d %H:%M:%S")
        info.info = sys.info()
        print(json.encode(info))
        last_30s = os.time()
    end
    sys.yield()
end