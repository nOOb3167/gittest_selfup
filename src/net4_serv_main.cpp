#include <cassert>
#include <cstdlib>
#include <cstring>

#include <string>

#include <gittest/misc.h>
#include <gittest/config.h>
#include <gittest/log.h>
#include <gittest/gittest.h>
#include <gittest/net4.h>

GsLogList *g_gs_log_list_global = gs_log_list_global_create();
GsCrashHandlerDumpExtraNet4 g_net4_crash_handler = {};

int main(int argc, char **argv)
{
	int r = 0;

	struct GsConfMap *ConfMap = NULL;
	struct GsAuxConfigCommonVars CommonVars = {};

	if (!!(r = aux_gittest_init()))
		GS_GOTO_CLEAN();

	if (!!(r = gs_log_crash_handler_setup()))
		GS_GOTO_CLEAN();

	if (!!(r = gs_config_read_default_everything(&ConfMap)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_config_get_common_vars(ConfMap, &CommonVars)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_config_create_common_logs(ConfMap)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_net4_crash_handler_init(
		NULL, 0,
		CommonVars.ServHostNameBuf, CommonVars.LenServHostName,
		std::to_string(CommonVars.ServPort).c_str(),
		&g_net4_crash_handler)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = gs_log_list_set_func_dump_extra(GS_LOG_LIST_GLOBAL_NAME, gs_net4_crash_handler_log_dump_extra_func, &g_net4_crash_handler.base)))
		GS_GOTO_CLEAN();

	{
		log_guard_t Log(GS_LOG_GET("serv"));

		if (!!(r = gs_net4_serv_start(&CommonVars)))
			GS_GOTO_CLEAN();
	}

clean:
	GS_DELETE_F(&ConfMap, gs_conf_map_destroy);

	gs_log_crash_handler_dump_global_log_list_suffix("_log", strlen("_log"));

	if (!!r)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
