import asyncio
import websockets
import requests
import time
import re
import urllib.parse
import logging
import logging.handlers
import json
import datetime

logger = logging.getLogger(__name__)


def init_logger(file_name):
    formatter = logging.Formatter('[%(asctime)s] <%(threadName)s> %(levelname)s - %(message)s')
    handler = logging.handlers.RotatingFileHandler(file_name, maxBytes=100000, backupCount=3)
    handler.setFormatter(formatter)
    log = logging.getLogger('')
    log.setLevel(logging.INFO)
    log.addHandler(handler)


def classify_message(msg):
    j = json.loads(msg)
    if not j:
        return 'keep-alive'
    if 'M' in j:
        for m in j['M']:
            if 'M' in m:
                return m['M']
    return 'other'


def find_coupons(coupon_names, msg):
    """
    Message format is as follows.
    Note that there are some irregularities. For example, the 'A' field value is sometimes a list with a single item
    and other times a list of list of items.
    {
      ...
      'M': [
        ...
        'A': [
          [
            {
              ...
              "couponName": "...",
              "openPrice": ...,
              "currentPrice": ...
            }
          ]
        ]
      ]
    }
    """
    j = json.loads(msg)
    coupons = {}
    if 'M' in j:
        for m in j['M']:
            if 'A' in m:
                for str in m['A']:
                    items = json.loads(str)
                    if isinstance(items, dict):
                        items = [items]
                    for item in items:
                        name = item.get('couponName')
                        if name in coupon_names:
                            coupon = {
                                'name': name,
                                'openPrice': item['openPrice'],
                                'currentPrice': item['currentPrice'],
                                'previousClose': item['previousClose']
                            }
                            coupons[name] = coupon
    return coupons


async def websock(url_ws, send_messages, token, coupon_names, callback):
    uri = url_ws.format(token=token)
    async with websockets.connect(uri) as websocket:
        for m in send_messages:
            await websocket.send(m)
        while True:
            msg = await websocket.recv()
            logger.info(f'received websocket message, type={classify_message(msg)}')
            coupons = find_coupons(coupon_names, msg)
            if coupons:
                callback(coupons)


def get_token(url_token):
    now = int(time.time() * 1000)
    url = url_token.format(now=now)
    res = requests.get(url)
    pattern = r'.*"ConnectionToken":"([^"]+)".*'
    m = re.match(pattern, res.text)
    return urllib.parse.quote_plus(m.group(1))


def init_websock(url_token, url_ws, send_messages, coupon_names, callback):
    while True:
        token = get_token(url_token)
        try:
            asyncio.get_event_loop().run_until_complete(websock(url_ws, send_messages, token, coupon_names, callback))
        except:
            logger.exception('error occurred in web socket loop, backing off then retrying')
        time.sleep(10)


def write_coupons_messages(coupon_names, coupons):
    date = datetime.datetime.now().strftime('%a %I:%M %p').replace(' 0', ' ')
    lines = [date]
    for name in coupon_names:
        coupon = coupons.get(name)
        if coupon:
            change = int(round((coupon['currentPrice'] - coupon['previousClose']) * 100.0))
            up = "++" if change >= 0 else "-"
            lines.append(f'{up}{change} bp {coupon["name"]} {coupon["currentPrice"]:.2f}')
    with open('coupons.txt', 'w') as f:
        f.write('\n'.join(lines))
    logger.info('updated coupons.txt')


def load_config():
    with open('config.json') as f:
        return json.load(f)


def main():
    config = load_config()

    init_logger('coupons.log')
    logger.info('started coupons agent')

    coupons = {}

    def callback(c):
        logger.info(list(c.values()))
        coupons.update(c)
        write_coupons_messages(config['coupon_names'], coupons)

    init_websock(config['url_token'], config['url_ws'], config['messages'], config['coupon_names'], callback)


if __name__ == '__main__':
    main()
