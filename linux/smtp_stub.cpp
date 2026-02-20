/* Stub implementation of SMTP API for native Linux build.
 * Mail is not sent; MailInit and SendMail no-op. */

#include "platform/pdw_linux_types.h"
#include "utils/smtp.h"
#include <stddef.h>

char *szSmtpCharSets[MAX_SMTP_CHARSETS];

int MailInit(char *szMailHost, char *szMailHeloDomain, char *szMailFrom, char *szMailTo, char *szMailUser, char *szMailPassword, int iMailPort, int nOptions)
{
	(void)szMailHost;
	(void)szMailHeloDomain;
	(void)szMailFrom;
	(void)szMailTo;
	(void)szMailUser;
	(void)szMailPassword;
	(void)iMailPort;
	(void)nOptions;
	return 0;
}

int SendMail(HWND hResponse, bool bMatch, bool bMonitor_only, int iSeparateSMTP, char *sz1, char *sz2, char *sz3, char *sz4, char *sz5, char *sz6, char *sz7, char *szLabel)
{
	(void)hResponse;
	(void)bMatch;
	(void)bMonitor_only;
	(void)iSeparateSMTP;
	(void)sz1;
	(void)sz2;
	(void)sz3;
	(void)sz4;
	(void)sz5;
	(void)sz6;
	(void)sz7;
	(void)szLabel;
	return 0;
}
