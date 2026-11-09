#include "request.h"

Err VmRequestRun(const VmRequestLink *l, UInt16 tries, Boolean *linkGone)
{
	Err err = vmErrNoReply;
	UInt16 t = 0, unsent = 0;

	*linkGone = 0;
	while (t < tries) {
		if (l->cancelled(l->ctx))
			return vmErrCancelled;
		l->drain(l->ctx);
		err = l->send(l->ctx);
		if (err == serErrTimeOut && ++unsent < VM_UNSENT_TRIES)
			continue;
		if (err != errNone)
			break;
		err = l->wait(l->ctx);
		if (err != vmErrNoReply)
			return err;
		t++;
	}
	if (err == serErrTimeOut || err == vmErrPartSent)
		err = vmErrNoReply;
	*linkGone = 1;
	return err;
}
