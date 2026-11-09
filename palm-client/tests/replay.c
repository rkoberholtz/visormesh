/*
 * Replays frames captured by the bridge (tools/visor_bridge.py --capture)
 * through the app's parsers: every radio->visor frame whose code the app
 * parses must parse. Catches a firmware update that changes a layout before
 * it reaches the Visor. Captures hold real messages, so they stay local.
 *
 *     make replay                      the bridge's default capture
 *     make replay CAPTURE=frames.txt
 *
 * Each capture line: "HH:MM:SS radio->visor <hex>" (or visor->radio, skipped).
 */
#include <stdio.h>
#include <string.h>
#include "../src/protocol.h"
#include "../src/protocol2.h"
#include "../src/remote_fmt.h"
#include "../src/scope_proto.h"

#define MAX_CODE 256

static int seen[MAX_CODE], bad[MAX_CODE], skipped[MAX_CODE];

static const char *Name(int code)
{
	switch (code) {
	case VM_RESP_CONTACTS_START:     return "CONTACTS_START";
	case VM_RESP_CONTACT:            return "CONTACT";
	case VM_RESP_END_OF_CONTACTS:    return "END_OF_CONTACTS";
	case VM_RESP_SELF_INFO:          return "SELF_INFO";
	case VM_RESP_SENT:               return "SENT";
	case VM_RESP_CURR_TIME:          return "CURR_TIME";
	case VM_RESP_DEVICE_INFO:        return "DEVICE_INFO";
	case VM_RESP_CONTACT_MSG_V3:     return "CONTACT_MSG_V3";
	case VM_RESP_CHANNEL_MSG_V3:     return "CHANNEL_MSG_V3";
	case VM_RESP_CHANNEL_INFO:       return "CHANNEL_INFO";
	case VM_RESP_BATT_AND_STORAGE:   return "BATT_AND_STORAGE";
	case VM_RESP_STATS:              return "STATS";
	case VM_RESP_DEFAULT_FLOOD_SCOPE: return "DEFAULT_FLOOD_SCOPE";
	case VM_PUSH_SEND_CONFIRMED:     return "SEND_CONFIRMED";
	case VM_PUSH_LOGIN_SUCCESS:      return "LOGIN_SUCCESS";
	case VM_PUSH_LOGIN_FAIL:         return "LOGIN_FAIL";
	case VM_PUSH_STATUS_RESPONSE:    return "STATUS_RESPONSE";
	case VM_PUSH_LOG_RX:             return "LOG_RX";
	case VM_PUSH_TRACE_DATA:         return "TRACE_DATA";
	case VM_PUSH_TELEMETRY_RESPONSE: return "TELEMETRY_RESPONSE";
	case VM_PUSH_BINARY_RESPONSE:    return "BINARY_RESPONSE";
	default:                         return NULL;
	}
}

/* 1 parsed, 0 failed, -1 a code the app doesn't parse (OK, ERR, adverts, ...). */
static int Parse(const UInt8 *p, UInt16 len)
{
	static VmDeviceInfo di;
	static VmSelfInfo si;
	static VmContact c;
	static VmMessage m;
	static VmChannel ch;
	static VmSent s;
	static VmRxLog rx;
	static VmTrace tr;
	static VmBattery b;
	static VmStats st;
	static VmRepeaterStats rs;
	UInt32 u32, u32b;
	UInt8 prefix[6], key[VM_SCOPE_KEY_SIZE];
	Boolean ok, admin;
	const UInt8 *data;
	UInt16 n;
	Char name[64];

	switch (p[0]) {
	case VM_RESP_CONTACTS_START:    return VmParseContactsStart(p, len, &u32);
	case VM_RESP_CONTACT:           return VmParseContact(p, len, &c);
	case VM_RESP_END_OF_CONTACTS:   return VmParseEndOfContacts(p, len, &u32);
	case VM_RESP_SELF_INFO:         return VmParseSelfInfo(p, len, &si);
	case VM_RESP_SENT:              return VmParseSent(p, len, &s);
	case VM_RESP_CURR_TIME:         return VmParseCurrTime(p, len, &u32);
	case VM_RESP_DEVICE_INFO:       return VmParseDeviceInfo(p, len, &di);
	case VM_RESP_CONTACT_MSG_V3:
	case VM_RESP_CHANNEL_MSG_V3:    return VmParseMessage(p, len, &m);
	case VM_RESP_CHANNEL_INFO:      return VmParseChannelInfo(p, len, &ch);
	case VM_RESP_BATT_AND_STORAGE:  return VmParseBattery(p, len, &b);
	case VM_RESP_STATS:             memset(&st, 0, sizeof(st)); return VmParseStats(p, len, &st);
	case VM_RESP_DEFAULT_FLOOD_SCOPE: return VmParseDefaultScope(p, len, name, key);
	case VM_PUSH_SEND_CONFIRMED:    return VmParseSendConfirmed(p, len, &u32, &u32b);
	case VM_PUSH_LOGIN_SUCCESS:
	case VM_PUSH_LOGIN_FAIL:        return VmParseLoginResult(p, len, &ok, &admin, prefix);
	case VM_PUSH_STATUS_RESPONSE:   return VmParseStatus(p, len, prefix, &rs);
	case VM_PUSH_LOG_RX:            return VmParseRxLog(p, len, &rx);
	case VM_PUSH_TRACE_DATA:        return VmParseTrace(p, len, &tr);
	case VM_PUSH_TELEMETRY_RESPONSE: return VmParseTelemetry(p, len, prefix, &data, &n);
	case VM_PUSH_BINARY_RESPONSE:   return VmParseRegionsReply(p, len, &u32, &data, &n);
	default:                        return -1;
	}
}

static int Hex(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

int main(int argc, char **argv)
{
	static char line[4 * VM_MAX_FRAME];
	static UInt8 frame[VM_MAX_FRAME + 8];
	FILE *f = argc > 1 ? fopen(argv[1], "r") : stdin;
	int lines = 0, frames = 0, failures = 0, code, r;
	UInt16 len;
	char *h;

	if (!f) {
		printf("replay: can't open %s (start the bridge to capture: tools/bridge.sh restart)\n", argv[1]);
		return 1;
	}
	while (fgets(line, sizeof(line), f)) {
		lines++;
		h = strstr(line, "radio->visor ");
		if (!h)
			continue;
		h += strlen("radio->visor ");
		for (len = 0; Hex(h[0]) >= 0 && Hex(h[1]) >= 0 && len < sizeof(frame); h += 2)
			frame[len++] = (UInt8)(Hex(h[0]) << 4 | Hex(h[1]));
		if (len == 0)
			continue;
		frames++;
		code = frame[0];
		seen[code]++;
		r = Parse(frame, len);
		if (r < 0) {
			skipped[code]++;
		} else if (!r) {
			bad[code]++;
			if (bad[code] <= 3)
				printf("FAIL %s (0x%02x), %u bytes, capture line %d\n", Name(code), code, len, lines);
			failures++;
		}
	}
	for (code = 0; code < MAX_CODE; code++)
		if (seen[code] && !skipped[code])
			printf("  %-20s %5d frames, %d failed\n", Name(code), seen[code], bad[code]);
	printf("replay: %d frames, %d failed to parse\n", frames, failures);
	return failures ? 1 : 0;
}
