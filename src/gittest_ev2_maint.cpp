#include <cstdlib>
#include <cstring>

#include <gittest/misc.h>
#include <gittest/log.h>
#include <gittest/config.h>
#include <gittest/filesys.h>
#include <gittest/gittest.h>

#define GS_REPO_SETUP_ARG_UPDATEMODE            "--gsreposetup"
#define GS_REPO_SETUP_ARG_DUMMYPREP             "--xdummyprep"

GsLogList *g_gs_log_list_global = gs_log_list_global_create();

int gs_ev2_maint_main_mode_dummyprep(
	const char *RepoMainPathBuf, size_t LenRepoMainPath,
	const char *RepoSelfUpdatePathBuf, size_t LenRepoSelfUpdatePath,
	const char *RepoMasterUpdatePathBuf, size_t LenRepoMasterUpdatePath,
	const char *RefNameMainBuf, size_t LenRefNameMain,
	const char *RefNameSelfUpdateBuf, size_t LenRefNameSelfUpdate,
	const char *MainDirectoryFileNameBuf, size_t LenMainDirectoryFileName,
	const char *SelfUpdateExePathBuf, size_t LenSelfUpdateExePath,
	const char *SelfUpdateBlobNameBuf, size_t LenSelfUpdateBlobName,
	const char *MaintenanceBkpPathBuf, size_t LenMaintenanceBkpPath)
{
	int r = 0;

	size_t IsDir1 = 0;
	size_t IsDir2 = 0;

	git_repository *RepoMain = NULL;
	git_repository *RepoSelfUpdate = NULL;
	git_repository *RepoMasterUpdate = NULL;

	git_oid BlobSelfUpdateOid = {};
	git_oid TreeOid = {};
	git_oid TreeMainOid = {};
	git_oid TreeSelfUpdateOid = {};
	git_oid CommitOid = {};
	git_oid CommitMainOid = {};
	git_oid CommitSelfUpdateOid = {};

	if (!!(r = gs_directory_create_unless_exist(MaintenanceBkpPathBuf, LenMaintenanceBkpPath)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_directory_create_unless_exist(MainDirectoryFileNameBuf, LenMainDirectoryFileName)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_file_is_directory(MainDirectoryFileNameBuf, LenMainDirectoryFileName, &IsDir1)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_file_is_directory(SelfUpdateExePathBuf, LenSelfUpdateExePath, &IsDir2)))
		GS_GOTO_CLEAN();

	if (!IsDir1 || IsDir2)
		GS_ERR_CLEAN(1);

	if (!!(r = gs_repo_init(RepoMainPathBuf, LenRepoMainPath, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_repo_init(RepoSelfUpdatePathBuf, LenRepoSelfUpdatePath, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_repo_init(RepoMasterUpdatePathBuf, LenRepoMasterUpdatePath, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_repository_open(RepoMainPathBuf, LenRepoMainPath, &RepoMain)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_repository_open(RepoSelfUpdatePathBuf, LenRepoSelfUpdatePath, &RepoSelfUpdate)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_repository_open(RepoMasterUpdatePathBuf, LenRepoMasterUpdatePath, &RepoMasterUpdate)))
		GS_GOTO_CLEAN();

	/* dummy clnt (RepoMasterUpdate @ main and selfupdate) */

	if (!!(r = clnt_tree_ensure_dummy(RepoMasterUpdate, &TreeOid)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_commit_ensure_dummy(RepoMasterUpdate, &TreeOid, &CommitOid)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_commit_setref(RepoMasterUpdate, RefNameMainBuf, &CommitOid)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_commit_setref(RepoMasterUpdate, RefNameSelfUpdateBuf, &CommitOid)))
		GS_GOTO_CLEAN();

	/* nondummy serv (RepoMain @ main and RepoSelfUpdate @ selfupdate) */

	/*   RepoMain */

	if (!!(r = clnt_tree_ensure_from_workdir(
		RepoMain,
		MainDirectoryFileNameBuf, LenMainDirectoryFileName,
		&TreeMainOid)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = clnt_commit_ensure_dummy(RepoMain, &TreeMainOid, &CommitMainOid)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_commit_setref(RepoMain, RefNameMainBuf, &CommitMainOid)))
		GS_GOTO_CLEAN();

	/*   RepoSelfUpdate */

	if (!!(r = git_blob_create_fromdisk(&BlobSelfUpdateOid, RepoSelfUpdate, SelfUpdateExePathBuf)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_tree_ensure_single(
		RepoSelfUpdate,
		SelfUpdateBlobNameBuf, LenSelfUpdateBlobName,
		&BlobSelfUpdateOid,
		&TreeSelfUpdateOid)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = clnt_commit_ensure_dummy(RepoSelfUpdate, &TreeSelfUpdateOid, &CommitSelfUpdateOid)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_commit_setref(RepoSelfUpdate, RefNameSelfUpdateBuf, &CommitSelfUpdateOid)))
		GS_GOTO_CLEAN();

	/* maintenance */

	if (!!(r = aux_repository_maintenance_special(
		GS_ARGOWN(&RepoMain),
		RepoMainPathBuf, LenRepoMainPath,
		MaintenanceBkpPathBuf, LenMaintenanceBkpPath)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = aux_repository_maintenance_special(
		GS_ARGOWN(&RepoSelfUpdate),
		RepoSelfUpdatePathBuf, LenRepoSelfUpdatePath,
		MaintenanceBkpPathBuf, LenMaintenanceBkpPath)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = aux_repository_maintenance_special(
		GS_ARGOWN(&RepoMasterUpdate),
		RepoMasterUpdatePathBuf, LenRepoMasterUpdatePath,
		MaintenanceBkpPathBuf, LenMaintenanceBkpPath)))
	{
		GS_GOTO_CLEAN();
	}

clean:
	git_repository_free(RepoMain);
	git_repository_free(RepoSelfUpdate);
	git_repository_free(RepoMasterUpdate);

	return r;
}

int gs_ev2_maint_main(
	int argc, char **argv,
	struct GsAuxConfigCommonVars *CommonVars)
{
	int r = 0;

	GS_LOG(I, S, "start");

	if (argc < 2)
		GS_ERR_NO_CLEAN_L(0, I, PF, "no action performed [argc=%d]", argc);

	if (strcmp(argv[1], GS_REPO_SETUP_ARG_UPDATEMODE) != 0)
		GS_ERR_NO_CLEAN_L(0, I, PF, "no update done ([arg=%s])", argv[1]);

	if (argc < 3)
		GS_ERR_CLEAN_L(1, I, PF, "args ([argc=%d])", argc);

	if (strcmp(argv[2], GS_REPO_SETUP_ARG_DUMMYPREP) == 0) {
		GS_LOG(I, S, "dummyprep start");
		if (argc != 3)
			GS_ERR_CLEAN(1);
		if (!!(r = gs_ev2_maint_main_mode_dummyprep(
			CommonVars->RepoMainPathBuf, CommonVars->LenRepoMainPath,
			CommonVars->RepoSelfUpdatePathBuf, CommonVars->LenRepoSelfUpdatePath,
			CommonVars->RepoMasterUpdatePathBuf, CommonVars->LenRepoMasterUpdatePath,
			CommonVars->RefNameMainBuf, CommonVars->LenRefNameMain,
			CommonVars->RefNameSelfUpdateBuf, CommonVars->LenRefNameSelfUpdate,
			CommonVars->MainDirPathBuf, CommonVars->LenMainDirPath,
			CommonVars->SelfUpdateExePathBuf, CommonVars->LenSelfUpdateExePath,
			CommonVars->SelfUpdateBlobNameBuf, CommonVars->LenSelfUpdateBlobName,
			CommonVars->MaintenanceBkpPathBuf, CommonVars->LenMaintenanceBkpPath)))
		{
			GS_GOTO_CLEAN();
		}
	} else {
		GS_LOG(I, PF, "unrecognized argument [%.s]", argv[2]);
		GS_ERR_CLEAN(1);
	}

noclean:

clean:

	return r;
}

int main(int argc, char **argv)
{
	int r = 0;

	struct GsConfMap *ConfMap = NULL;

	struct GsAuxConfigCommonVars CommonVars = {};

	if (!!(r = aux_gittest_init()))
		GS_GOTO_CLEAN();

	if (!!(r = gs_log_crash_handler_setup()))
		GS_GOTO_CLEAN();

	GS_LOG_ADD(gs_log_base_create_ret("repo_setup"));

	if (!!(r = gs_config_read_default_everything(&ConfMap)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_config_get_common_vars(ConfMap, &CommonVars)))
		GS_GOTO_CLEAN();

	{
		log_guard_t log(GS_LOG_GET("repo_setup"));

		if (!!(r = gs_ev2_maint_main(argc, argv,
			&CommonVars)))
		{
			GS_GOTO_CLEAN();
		}
	}

clean:
	GS_DELETE_F(&ConfMap, gs_conf_map_destroy);

	/* always dump logs. not much to do about errors here though */
	gs_log_crash_handler_dump_global_log_list_suffix("_log", strlen("_log"));

	if (!!r)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
