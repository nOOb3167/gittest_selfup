#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <thread>
#include <sstream>

#include <git2.h>

#include <gittest/misc.h>
#include <gittest/config.h>
#include <gittest/log.h>
#include <gittest/filesys.h>
#include <gittest/gittest.h>
#include <gittest/gittest_ev2_test.h>
#include <gittest/gittest_ev2_test_c.h>

GsLogList *g_gs_log_list_global = gs_log_list_global_create();

int gs_ev2_selfupdate_reexec()
{
	int r = 0;

	char CurExeBuf[512] = {};
	size_t LenCurExe = 0;

	std::stringstream ss;
	std::string out;

	if (!!(r = gs_get_current_executable_filename(CurExeBuf, sizeof CurExeBuf, &LenCurExe)))
		GS_GOTO_CLEAN();

	ss << std::string(CurExeBuf, LenCurExe) << "\0" << std::string(GS_SELFUPDATE_ARG_CHILD) << "\0";
	out = ss.str();

	if (!!(r = gs_process_start_ex(
		CurExeBuf, LenCurExe,
		out.data(), out.size())))
	{
		GS_GOTO_CLEAN();
	}

clean:

	return r;
}

int gs_ev2_selfupdate_dryrun(
	char *RunFileNameBuf, size_t LenRunFileName)
{
	int r = 0;

	std::stringstream ss;
	std::string out;

	ss << std::string(RunFileNameBuf, LenRunFileName) << "\0" << std::string(GS_SELFUPDATE_ARG_VERSUB) << "\0";
	out = ss.str();

	if (!!(r = gs_process_start_ex(
		RunFileNameBuf, LenRunFileName,
		out.data(), out.size())))
	{
		GS_GOTO_CLEAN();
	}

clean:

	return r;
}

int gs_ev2_selfupdate_full(
	struct GsAuxConfigCommonVars CommonVars,
	uint32_t *oHaveUpdateShouldQuit)
{
	int r = 0;

	uint32_t HaveUpdateShouldQuit = 0;

	struct GsEvCtxSelfUpdate *Ctx = NULL;

	uint32_t Code = 0;

	char CurExeBuf[512] = {};
	size_t LenCurExe = 0;

	char TempFileNameBuf[512] = {};
	size_t LenTempFileName = 0;

	char OldFileNameBuf[512] = {};
	size_t LenOldFileName = 0;

	if (!!(r = gs_get_current_executable_filename(CurExeBuf, sizeof CurExeBuf, &LenCurExe)))
		GS_GOTO_CLEAN();

	GS_LOG(I, PF, "start selfupdate [executable_filename=[%.*s]]", LenCurExe, CurExeBuf);

	if (!!(r = gs_ev2_test_selfupdatemain(CommonVars, CurExeBuf, LenCurExe, &Ctx)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_selfupdate_state_code(Ctx->mState, &Code)))
		GS_GOTO_CLEAN();

	GS_LOG(I, PF, "finish selfupdate [code=%d]", (int) Code);

	if (Code == GS_SELFUPDATE_STATE_CODE_NEED_BLOB_HEAD)
		GS_ERR_NO_CLEAN(0);  // exited upon finding gotten HEAD needs no update triggered
	if (Code != GS_SELFUPDATE_STATE_CODE_NEED_NOTHING)
		GS_ERR_CLEAN(1);     // must be some kind of oversight

	GS_LOG(I, S, "start applying selfupdate");

	if (!!(r = gs_build_modified_filename(
		CurExeBuf, LenCurExe,
		"", 0,
		GS_STR_EXECUTABLE_EXPECTED_EXTENSION, strlen(GS_STR_EXECUTABLE_EXPECTED_EXTENSION),
		GS_STR_PARENT_EXTRA_SUFFIX, strlen(GS_STR_PARENT_EXTRA_SUFFIX),
		GS_STR_EXECUTABLE_EXPECTED_EXTENSION, strlen(GS_STR_EXECUTABLE_EXPECTED_EXTENSION),
		TempFileNameBuf, sizeof TempFileNameBuf, &LenTempFileName)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = gs_build_modified_filename(
		CurExeBuf, LenCurExe,
		"", 0,
		GS_STR_EXECUTABLE_EXPECTED_EXTENSION, strlen(GS_STR_EXECUTABLE_EXPECTED_EXTENSION),
		GS_STR_PARENT_EXTRA_SUFFIX_OLD, strlen(GS_STR_PARENT_EXTRA_SUFFIX_OLD),
		GS_STR_EXECUTABLE_EXPECTED_EXTENSION, strlen(GS_STR_EXECUTABLE_EXPECTED_EXTENSION),
		OldFileNameBuf, sizeof OldFileNameBuf, &LenOldFileName)))
	{
		GS_GOTO_CLEAN();
	}

	GS_LOG(I, PF, "temp_filename=[%.*s]", LenTempFileName, TempFileNameBuf);

	GS_LOG(I, PF, "old_filename=[%.*s]", LenOldFileName, OldFileNameBuf);

	if (!!(r = gs_file_write_frombuffer(
		TempFileNameBuf, LenTempFileName,
		(uint8_t *) Ctx->mState->mBufferUpdate->data(), Ctx->mState->mBufferUpdate->size())))
	{
		GS_GOTO_CLEAN();
	}

	GS_LOG(I, S, "dryrun");

	if (!!(r = gs_ev2_selfupdate_dryrun(TempFileNameBuf, LenTempFileName)))
		GS_GOTO_CLEAN();

	GS_LOG(I, S, "rename");

	if (!!(r = gs_rename_wrapper(
		CurExeBuf, LenCurExe,
		OldFileNameBuf, LenOldFileName)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = gs_rename_wrapper(
		TempFileNameBuf, LenTempFileName,
		CurExeBuf, LenCurExe)))
	{
		GS_GOTO_CLEAN();
	}

	GS_LOG(I, S, "finish applying selfupdate");

	HaveUpdateShouldQuit = 1;

noclean:
	if (oHaveUpdateShouldQuit)
		*oHaveUpdateShouldQuit = HaveUpdateShouldQuit;

clean:
	GS_DELETE_F(&Ctx, gs_ev_ctx_selfupdate_destroy);

	return r;
}

int gs_ev2_mainupdate_full(
	struct GsAuxConfigCommonVars CommonVars)
{
	int r = 0;

	struct GsEvCtxClnt *Ctx = NULL;

	uint32_t Code = 0;

	GS_LOG(I, S, "start mainupdate");

	if (!!(r = gs_ev2_test_clntmain(CommonVars, &Ctx)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_state_code(Ctx->mClntState, &Code)))
		GS_GOTO_CLEAN();

	GS_LOG(I, PF, "finish mainupdate [code=%d]", (int) Code);

	if (Code == GS_CLNT_STATE_CODE_NEED_TREE_HEAD)
		GS_ERR_NO_CLEAN(0);  // exited upon finding gotten HEAD needs no update triggered
	if (Code != GS_CLNT_STATE_CODE_NEED_NOTHING)
		GS_ERR_CLEAN(1);     // must be some kind of oversight

noclean:

clean:
	GS_DELETE_F(&Ctx, gs_ev_ctx_clnt_destroy);

	return r;
}

int gs_ev2_selfupdate_checkout(
	struct GsAuxConfigCommonVars CommonVars)
{
	int r = 0;

	GS_LOG(I, S, "start checkout");

	if (!!(r = aux_repository_checkout(
		CommonVars.RepoMasterUpdatePathBuf, CommonVars.LenRepoMasterUpdatePath,
		CommonVars.RefNameMainBuf, CommonVars.LenRefNameMain,
		CommonVars.RepoMasterUpdateCheckoutPathBuf, CommonVars.LenRepoMasterUpdateCheckoutPath)))
	{
		GS_GOTO_CLEAN();
	}

	GS_LOG(I, S, "finish checkout");

clean:

	return r;
}

int main(int argc, char **argv)
{
	int r = 0;

	struct GsConfMap *ConfMap = NULL;
	struct GsAuxConfigCommonVars CommonVars = {};

	uint32_t HaveUpdateShouldQuit = 0;
	uint32_t DoNotReExec = 0;

	// FIXME: due to possible race condition vs startup of server (workaround)
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

#ifdef _WIN32
	/* Grrr. Needed for LibEvent.
	   Hopefully some header defines WSAStartup for us. */
	WSADATA wsa_data;
	if (!! WSAStartup(0x0201, &wsa_data))
		GS_ERR_CLEAN(1);
#endif

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

	if (argc == 2 && strcmp(argv[1], GS_SELFUPDATE_ARG_VERSUB) == 0) {
		printf(GS_CONFIG_DEFS_GITTEST_EV2_SELFUPDATE_VERSUB);
		GS_ERR_NO_CLEAN(0);
	}
	else if (argc == 2 && strcmp(argv[1], GS_SELFUPDATE_ARG_CHILD) == 0) {
		DoNotReExec = 1;
	}

	{
		log_guard_t Log(GS_LOG_GET("selfup"));

		if (!!(r = gs_ev2_selfupdate_full(CommonVars, &HaveUpdateShouldQuit)))
			GS_GOTO_CLEAN();

		if (HaveUpdateShouldQuit) {
			if (DoNotReExec)
				GS_ERR_CLEAN(1);
			if (!!(r = gs_ev2_selfupdate_reexec()))
				GS_GOTO_CLEAN();
		}
		else {
			if (!!(r = gs_ev2_mainupdate_full(CommonVars)))
				GS_GOTO_CLEAN();

			if (!!(r = gs_ev2_selfupdate_checkout(CommonVars)))
				GS_GOTO_CLEAN();
		}
	}

noclean:

clean:
	GS_DELETE_F(&ConfMap, gs_conf_map_destroy);

	gs_log_crash_handler_dump_global_log_list_suffix("_log", strlen("_log"));

	if (!!r)
		EXIT_FAILURE;

	return EXIT_SUCCESS;
}
