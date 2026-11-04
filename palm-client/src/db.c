#include <PalmOS.h>
#include "app.h"
#include "db.h"

Err DbOpen(const Char *name, UInt32 type, DmOpenRef *db)
{
	LocalID id = DmFindDatabase(0, name);
	UInt16 attrs;
	Err err;

	*db = 0;
	if (!id) {
		err = DmCreateDatabase(0, name, VM_CREATOR, type, false);
		if (err != errNone)
			return err;
		id = DmFindDatabase(0, name);
		if (!id)
			return DmGetLastErr();
		DmDatabaseInfo(0, id, NULL, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		attrs |= dmHdrAttrBackup;
		DmSetDatabaseInfo(0, id, NULL, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	}
	*db = DmOpenDatabase(0, id, dmModeReadWrite);
	return *db ? errNone : DmGetLastErr();
}
