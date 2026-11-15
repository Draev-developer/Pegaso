"""The local API: REST and MCP.

usage: python -I apitest.py [net]

Without "net" Pegaso stays offline: who is let in (token, Host, Origin), downloads (add an
ed2k link and a web address, list, pause, resume, remove), shared files, and the MCP
endpoint (initialize, tools/list, tools/call). With "net" it also joins Kad and runs a
search through the API.
"""
import http.client
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import nettest  # noqa: E402
import ratetest as r  # noqa: E402

PORT = 4791
TOKEN = '0123456789abcdef0123456789abcdef'
NAME = 'Video.prueba.avi.exe'  # a name the fake detector objects to
SIZE = 5 * 1024 * 1024
# the version the build announces: the one of the installer script
VERSION = [line.split('"')[1] for line in open(os.path.join(HERE, '..', 'installer', 'pegaso.iss'), encoding='utf-8-sig') if line.startswith('#define AppVersion')][0]


def call(method, path, body=None, token=TOKEN, headers=None):
    conn = http.client.HTTPConnection('127.0.0.1', PORT, timeout=40)
    head = {'Content-Type': 'application/json'}
    if token:
        head['Authorization'] = 'Bearer ' + token
    head.update(headers or {})
    conn.request(method, path, json.dumps(body).encode() if body is not None else None, head)
    resp = conn.getresponse()
    raw = resp.read().decode('utf-8')
    conn.close()
    return resp.status, (json.loads(raw) if raw else None)


def mcp(method, params=None, msg_id=1):
    msg = {'jsonrpc': '2.0', 'method': method}
    if msg_id is not None:
        msg['id'] = msg_id
    if params is not None:
        msg['params'] = params
    return call('POST', '/mcp', msg)


def tool(name, arguments=None):
    status, answer = mcp('tools/call', {'name': name, 'arguments': arguments or {}}, 7)
    result = answer['result']
    text = result['content'][0]['text']
    return result['isError'], (json.loads(text) if not result['isError'] else text)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    net = len(sys.argv) > 1 and sys.argv[1] == 'net'
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-66s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\n' % (PORT, TOKEN))
    if net:
        ini = os.path.join(w.RUN, 'config', 'preferences.ini')
        text = open(ini).read().replace('NetworkKademlia=0\n', '')
        open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    server = w.start_server()
    pegaso = r.start(w.RUN)
    try:
        print('who is let in')
        check('without a token: 401', call('GET', '/api/v1/status', token=None)[0] == 401)
        check('a wrong token: 401', call('GET', '/api/v1/status', token='f' * 32)[0] == 401)
        check('another Host (a web page pointing a name here): 403', call('GET', '/api/v1/status', headers={'Host': 'example.org'})[0] == 403)
        check('an Origin which is a web site: 403', call('GET', '/api/v1/status', headers={'Origin': 'https://example.org'})[0] == 403)
        check('a local Origin is fine', call('GET', '/api/v1/status', headers={'Origin': 'http://localhost:3000'})[0] == 200)
        import socket
        lan = r.lan_ip()
        try:
            socket.create_connection((lan, PORT), timeout=2).close()
            reachable = True
        except OSError:
            reachable = False
        check('not reachable through the network address (%s)' % lan, not reachable)

        print('REST')
        status, st = call('GET', '/api/v1/status')
        print('   ', st)
        check('status', status == 200 and st['name'] == 'Pegaso' and st['version'] == VERSION and st['ed2k']['connected'] is False)
        link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, w.ed2k_hash(os.urandom(SIZE)))
        status, added = call('POST', '/api/v1/downloads', {'links': [link, w.BASE + '/slow.bin', 'ed2k://|file|broken']})
        print('   ', added)
        check('two links added, the broken one reported', status == 200 and added['added'] == 2 and len(added['errors']) == 1)
        time.sleep(6)
        status, listed = call('GET', '/api/v1/downloads')
        names = {d['name']: d for d in listed['downloads']}
        print('   ', [(d['name'], d['kind'], d['origin'], d['trust']['label'], d['completed']) for d in listed['downloads']])
        check('both downloads listed', set(names) == {NAME, 'slow.bin'})
        fake = names.get(NAME, {})
        check('the fake detector is in the list', fake.get('trust', {}).get('score') == 85 and fake['trust']['reasons'])
        check('the web download receives data', names.get('slow.bin', {}).get('completed', 0) > 0 and names['slow.bin']['kind'] == 'web')
        h = fake.get('hash', '0' * 32)
        status, paused = call('POST', '/api/v1/downloads/%s/pause' % h)
        check('pause', status == 200 and paused['paused'] is True)
        status, resumed = call('POST', '/api/v1/downloads/%s/resume' % h)
        check('resume', status == 200 and resumed['paused'] is False)
        check('an unknown hash: 404', call('POST', '/api/v1/downloads/%s/pause' % ('ab' * 16))[0] == 404)
        status, removed = call('DELETE', '/api/v1/downloads/%s' % h)
        status2, listed = call('GET', '/api/v1/downloads')
        check('remove', status == 200 and removed['removed'] and NAME not in [d['name'] for d in listed['downloads']])
        status, shared = call('GET', '/api/v1/shared')
        check('shared files', status == 200 and shared['total'] == 0 and shared['files'] == [])
        check('a body which is not JSON: 400', call('POST', '/api/v1/downloads', None, headers={'Content-Length': '0'})[0] == 400
              or call('POST', '/api/v1/downloads', {})[0] == 400)
        check('an unknown address: 404', call('GET', '/api/v1/nothing')[0] == 404)
        if not net:
            status, err = call('POST', '/api/v1/searches', {'query': 'prueba'})
            check('a search while offline: 409 with a reason', status == 409 and err.get('error'), err)

        print('MCP')
        status, init = mcp('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'apitest', 'version': '1'}})
        print('   ', init)
        check('initialize', status == 200 and init['id'] == 1 and init['result']['protocolVersion'] == '2025-06-18'
              and init['result']['serverInfo']['name'] == 'pegaso' and 'tools' in init['result']['capabilities'])
        status, none = mcp('notifications/initialized', msg_id=None)
        check('a notification gets 202 and no body', status == 202 and none is None)
        status, tools = mcp('tools/list', msg_id='abc')
        listed_tools = [t['name'] for t in tools['result']['tools']]
        print('   ', listed_tools)
        check('tools/list (string id echoed, every tool with a schema)', tools['id'] == 'abc' and len(listed_tools) == 12
              and all(t['inputSchema']['type'] == 'object' and t['description'] for t in tools['result']['tools']))
        is_error, st = tool('pegaso_status')
        check('tools/call pegaso_status', not is_error and st['version'] == VERSION)
        is_error, added = tool('pegaso_add_download', {'links': [link]})
        check('tools/call pegaso_add_download', not is_error and added['added'] == 1)
        time.sleep(2)
        is_error, listed = tool('pegaso_list_downloads')
        check('tools/call pegaso_list_downloads', not is_error and NAME in [d['name'] for d in listed['downloads']])
        is_error, text = tool('pegaso_pause_download', {'hash': 'xyz'})
        check('a tool error is an error result, not a protocol error', is_error and 'hash' in text)
        status, unknown = mcp('tools/call', {'name': 'nope', 'arguments': {}})
        check('an unknown tool: JSON-RPC error', 'error' in unknown and unknown['error']['code'] == -32602)
        status, unknown = mcp('resources/list')
        check('an unknown method: -32601', unknown['error']['code'] == -32601)
        status, ping = mcp('ping')
        check('ping', ping['result'] == {})

        if net:
            print('search on Kad (real network)')
            hwnd = u.main_window(pegaso.pid)
            u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
            t0 = time.time()
            while time.time() - t0 < 120 and not call('GET', '/api/v1/status')[1]['kad']['connected']:
                time.sleep(3)
            time.sleep(10)
            is_error, started = tool('pegaso_search', {'query': 'ubuntu', 'network': 'kad'})
            print('   ', started)
            check('search started', not is_error and started.get('id') is not None, started)
            results = {'total': 0}
            t0 = time.time()
            while not is_error and time.time() - t0 < 60 and results['total'] < 3:
                time.sleep(5)
                is_error, results = tool('pegaso_search_results', {'search_id': started['id']})
            for item in results.get('results', [])[:4]:
                print('   ', item['name'][:60], item['size'], item['sources'], item['trust']['label'])
            check('results arrive, each with link and trust', results['total'] >= 1
                  and all(x['link'].startswith('ed2k://') and 'score' in x['trust'] for x in results['results']))
        check('Pegaso still running', pegaso.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        server.kill()


if __name__ == '__main__':
    main()
