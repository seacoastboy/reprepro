/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2008 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "database.h"
#include "reference.h"
#include "binaries.h"
#include "sources.h"
#include "names.h"
#include "dirs.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "log.h"
#include "files.h"
#include "descriptions.h"
#include "target.h"
#include "packagedata.h"

static char *calc_identifier(const char *codename, component_t component, architecture_t architecture, packagetype_t packagetype) {
	assert (strchr(codename, '|') == NULL);
	assert (codename != NULL); assert (atom_defined(component));
	assert (atom_defined(architecture));
	assert (atom_defined(packagetype));
	if (packagetype == pt_udeb)
		return mprintf("u|%s|%s|%s", codename,
				atoms_components[component],
				atoms_architectures[architecture]);
	else
		return mprintf("%s|%s|%s", codename,
				atoms_components[component],
				atoms_architectures[architecture]);
}


static retvalue target_initialize(/*@dependant@*/struct distribution *distribution, component_t component, architecture_t architecture, packagetype_t packagetype, get_version getversion, get_installdata getinstalldata, get_architecture getarchitecture, get_filekeys getfilekeys, get_checksums getchecksums, get_sourceandversion getsourceandversion, do_reoverride doreoverride, do_retrack doretrack, complete_checksums docomplete, /*@null@*//*@only@*/char *directory, /*@dependent@*/const struct exportmode *exportmode, bool readonly, bool noexport, /*@out@*/struct target **d) {
	struct target *t;

	assert(exportmode != NULL);
	if (FAILEDTOALLOC(directory))
		return RET_ERROR_OOM;

	t = zNEW(struct target);
	if (FAILEDTOALLOC(t)) {
		free(directory);
		return RET_ERROR_OOM;
	}
	t->relativedirectory = directory;
	t->exportmode = exportmode;
	t->distribution = distribution;
	assert (atom_defined(component));
	t->component = component;
	assert (atom_defined(architecture));
	t->architecture = architecture;
	assert (atom_defined(packagetype));
	t->packagetype = packagetype;
	t->identifier = calc_identifier(distribution->codename,
			component, architecture, packagetype);
	if (FAILEDTOALLOC(t->identifier)) {
		(void)target_free(t);
		return RET_ERROR_OOM;
	}
	t->getversion = getversion;
	t->getinstalldata = getinstalldata;
	t->getarchitecture = getarchitecture;
	t->getfilekeys = getfilekeys;
	t->getchecksums = getchecksums;
	t->getsourceandversion = getsourceandversion;
	t->doreoverride = doreoverride;
	t->doretrack = doretrack;
	t->completechecksums = docomplete;
	t->readonly = readonly;
	t->noexport = noexport;
	*d = t;
	return RET_OK;
}

static const char *dist_component_name(component_t component, /*@null@*/const char *fakecomponentprefix) {
	const char *c = atoms_components[component];
	size_t len;

	if (fakecomponentprefix == NULL)
		return c;
	len = strlen(fakecomponentprefix);
	if (strncmp(c, fakecomponentprefix, len) != 0)
		return c;
	if (c[len] != '/')
		return c;
	return c + len + 1;
}

retvalue target_initialize_ubinary(struct distribution *d, component_t component, architecture_t architecture, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture, pt_udeb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			ubinaries_doreoverride, binaries_retrack,
			binaries_complete_checksums,
			mprintf("%s/debian-installer/binary-%s",
				dist_component_name(component,
					fakecomponentprefix),
				atoms_architectures[architecture]),
			exportmode, readonly, noexport, target);
}
retvalue target_initialize_binary(struct distribution *d, component_t component, architecture_t architecture, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture, pt_deb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			binaries_doreoverride, binaries_retrack,
			binaries_complete_checksums,
			mprintf("%s/binary-%s",
				dist_component_name(component,
					fakecomponentprefix),
				atoms_architectures[architecture]),
			exportmode, readonly, noexport, target);
}

retvalue target_initialize_source(struct distribution *d, component_t component, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture_source, pt_dsc,
			sources_getversion,
			sources_getinstalldata,
			sources_getarchitecture,
			sources_getfilekeys, sources_getchecksums,
			sources_getsourceandversion,
			sources_doreoverride, sources_retrack,
			sources_complete_checksums,
			mprintf("%s/source", dist_component_name(component,
					fakecomponentprefix)),
			exportmode, readonly, noexport, target);
}

retvalue target_free(struct target *target) {
	retvalue result = RET_OK;

	if (target == NULL)
		return RET_OK;
	if (target->packages != NULL) {
		result = target_closepackagesdb(target);
	} else
		result = RET_OK;
	if (target->wasmodified && !target->noexport) {
		fprintf(stderr,
"Warning: database '%s' was modified but no index file was exported.\n"
"Changes will only be visible after the next 'export'!\n",
				target->identifier);
	}

	target->distribution = NULL;
	free(target->identifier);
	free(target->relativedirectory);
	free(target);
	return result;
}

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *target, bool readonly) {
	retvalue r;

	if (!readonly && target->readonly) {
		fprintf(stderr,
"Error trying to open '%s' read-write in read-only distribution '%s'\n",
				target->identifier,
				target->distribution->codename);
		return RET_ERROR;
	}

	assert (target->packages == NULL);
	if (target->packages != NULL)
		return RET_OK;
	r = database_openpackages(target->identifier, readonly,
			&target->packages);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		target->packages = NULL;
		return r;
	}
	return r;
}

/* this closes databases... */
retvalue target_closepackagesdb(struct target *target) {
	retvalue r;

	if (target->packages == NULL) {
		fprintf(stderr, "Internal Warning: Double close!\n");
		r = RET_OK;
	} else {
		r = table_close(target->packages);
		target->packages = NULL;
	}
	return r;
}

/* Remove a package from the given target. */
retvalue target_removereadpackage(struct target *target, struct logger *logger, const char *name, const struct packagedata *olddata, struct trackingdata *trackingdata) {
	char *oldpversion = NULL;
	struct strlist files;
	retvalue result, r;
	char *oldsource, *oldsversion;
	char *key;

	assert (target != NULL && target->packages != NULL);
	assert (olddata != NULL && olddata->data != NULL && name != NULL);

	if (logger != NULL) {
		/* need to get the version for logging, if not available */
		r = target->getversion(olddata->chunk, &oldpversion);
		if (!RET_IS_OK(r))
			oldpversion = NULL;
	}
	r = target->getfilekeys(olddata->chunk, &files);
	if (RET_WAS_ERROR(r)) {
		free(oldpversion);
		return r;
	}
	if (trackingdata != NULL) {
		r = target->getsourceandversion(olddata->chunk,
				name, &oldsource, &oldsversion);
		if (!RET_IS_OK(r)) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if (verbose > 0)
		printf("removing '%s=%s' from '%s'...\n",
				name, olddata->version, target->identifier);
	key = packagedata_primarykey(name, olddata->version);
	result = table_deleterecord(target->packages, key, false);
	free(key);
	if (RET_IS_OK(result)) {
		target->wasmodified = true;
		if (oldsource!= NULL && oldsversion != NULL) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result, r);
		}
		if (trackingdata == NULL)
			target->staletracking = true;
		if (logger != NULL)
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, olddata->chunk,
					NULL, &files,
					NULL, NULL);
		r = references_delete(target->identifier, &files, NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	free(oldpversion);
	return result;
}

/* Remove a package from the given target. */
retvalue target_removepackage(struct target *target, struct logger *logger, const char *name, const char *version, struct trackingdata *trackingdata) {
	struct packagedata olddata;
	retvalue r;

	assert(target != NULL && target->packages != NULL && name != NULL);

	r = target_getpackage(target, name, version, &olddata);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	else if (r == RET_NOTHING) {
		if (verbose >= 10)
			fprintf(stderr, "Could not find '%s' in '%s'...\n",
					name, target->identifier);
		return RET_NOTHING;
	}
	r = target_removereadpackage(target, logger,
			name, &olddata, trackingdata);
	packagedata_free(&olddata);
	return r;
}


/* Like target_removepackage, but delete the package record by cursor */
retvalue target_removepackage_by_cursor(struct target_cursor *tc, struct logger *logger, struct trackingdata *trackingdata) {
	struct target * const target = tc->target;
	const char * const name = tc->lastname;
	struct packagedata packagedata;
	char *oldpversion = NULL;
	struct strlist files;
	retvalue result, r;
	char *oldsource, *oldsversion;

	parse_packagedata(tc->lastdata, tc->lastdata_len, &packagedata);

	assert (target != NULL && target->packages != NULL);
	assert (name != NULL && packagedata.data != NULL);

	if (logger != NULL) {
		/* need to get the version for logging, if not available */
		r = target->getversion(packagedata.chunk, &oldpversion);
		if (!RET_IS_OK(r))
			oldpversion = NULL;
	}
	r = target->getfilekeys(packagedata.chunk, &files);
	if (RET_WAS_ERROR(r)) {
		free(oldpversion);
		return r;
	}
	if (trackingdata != NULL) {
		r = target->getsourceandversion(packagedata.chunk,
				name, &oldsource, &oldsversion);
		if (!RET_IS_OK(r)) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if (verbose > 0)
		printf("removing '%s' from '%s'...\n",
				name, target->identifier);
	result = cursor_delete(target->packages, tc->cursor, tc->lastname, NULL);
	if (RET_IS_OK(result)) {
		target->wasmodified = true;
		if (oldsource != NULL && oldsversion != NULL) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result, r);
		}
		if (trackingdata == NULL)
			target->staletracking = true;
		if (logger != NULL)
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, packagedata.chunk,
					NULL, &files,
					NULL, NULL);
		r = references_delete(target->identifier, &files, NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	free(oldpversion);
	return result;
}

static retvalue addpackages(struct target *target, const char *packagename, const char *controlchunk, const char *version, /*@null@*/const struct packagedata *oldpackage, const struct strlist *files, /*@only@*//*@null@*/struct strlist *oldfiles, /*@null@*/struct logger *logger, /*@null@*/struct trackingdata *trackingdata, architecture_t architecture, /*@null@*//*@only@*/char *oldsource, /*@null@*//*@only@*/char *oldsversion, /*@null@*/const char *causingrule, /*@null@*/const char *suitefrom) {

	retvalue result, r;
	char *key;
	struct packagedata packagedata;
	struct table *table = target->packages;
	enum filetype filetype;

	assert (atom_defined(architecture));
	assert (oldpackage != NULL);

	if (architecture == architecture_source)
		filetype = ft_SOURCE;
	else if (architecture == architecture_all)
		filetype = ft_ALL_BINARY;
	else
		filetype = ft_ARCH_BINARY;

	/* mark it as needed by this distribution */

	r = references_insert(target->identifier, files, oldfiles);

	if (RET_WAS_ERROR(r)) {
		if (oldfiles != NULL)
			strlist_done(oldfiles);
		return r;
	}

	r = packagedata_create(version, controlchunk, &packagedata);
	if (RET_WAS_ERROR(r)) {
		if (oldfiles != NULL)
			strlist_done(oldfiles);
		return r;
	}

	/* Add package to the distribution's database */

	key = packagedata_primarykey(packagename, version);
	if (oldpackage->data != NULL) {
		result = table_replacesizedrecord(table, packagename, packagedata.data, packagedata.data_len);

	} else {
		result = table_adduniqsizedrecord(table, key, packagedata.data, packagedata.data_len, false, false);
	}
	free(key);

	if (RET_WAS_ERROR(result)) {
		if (oldfiles != NULL)
			strlist_done(oldfiles);
		return result;
	}

	if (logger != NULL)
		logger_log(logger, target, packagename,
				version, oldpackage->version,
				controlchunk, oldpackage->chunk,
				files, oldfiles, causingrule, suitefrom);

	r = trackingdata_insert(trackingdata, filetype, files,
			oldsource, oldsversion, oldfiles);
	RET_UPDATE(result, r);

	/* remove old references to files */

	if (oldfiles != NULL) {
		r = references_delete(target->identifier, oldfiles, files);
		RET_UPDATE(result, r);
		strlist_done(oldfiles);
	}

	return result;
}

// If version is NULL, return the latest version of the specified package name.
// The return structure packagedata must already be allocated.
retvalue target_getpackage(struct target *target, const char *name, const char *version, /*@out*/struct packagedata *packagedata) {
	void *data;
	size_t data_len;
	retvalue r;

	if (version == NULL) {
		r = table_getcomplexrecord(target->packages, true, name, &data, &data_len);
	} else {
		char *key;
		key = packagedata_primarykey(name, version);
		r = table_getcomplexrecord(target->packages, false, key, &data, &data_len);
		free(key);
	}
	if (!RET_IS_OK(r)) {
		// Error case or no package found.
		setzero(struct packagedata, packagedata);
		return r;
	}
	return parse_packagedata(data, data_len, packagedata);
}

retvalue target_addpackage(struct target *target, struct logger *logger, const char *name, const char *version, const char *control, const struct strlist *filekeys, bool downgrade, struct trackingdata *trackingdata, architecture_t architecture, const char *causingrule, const char *suitefrom, struct description *description) {
	struct strlist oldfilekeys, *ofk;
	struct packagedata oldpackage;
	char *newcontrol;
	char *oldsource, *oldsversion;
	retvalue r;
	// TODO: make keep_old user configurable.
	bool keep_old = true;
	// Overwrite existing package versions.
	bool overwrite_existing;
	bool replace = true;

	assert(target->packages!=NULL);

	overwrite_existing = downgrade;

	r = target_getpackage(target, name, NULL, &oldpackage);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
	} else {
		int versioncmp;

		r = dpkgversions_cmp(version, oldpackage.version, &versioncmp);
		if (RET_WAS_ERROR(r)) {
			if (!IGNORING(brokenversioncmp,
"Parse errors processing versions of %s.\n", name)) {
				packagedata_free(&oldpackage);
				return r;
			}
		} else {
			if (versioncmp == 0) {
				// new Version is same than the old version
				if (!overwrite_existing) {
					fprintf(stderr,
"Skipping inclusion of '%s' '%s' in '%s', as this version already exists.\n",
						name, version,
						target->identifier);
					packagedata_free(&oldpackage);
					return RET_NOTHING;
				} else {
					fprintf(stderr,
"Warning: replacing '%s' version '%s' with equal version '%s' in '%s'!\n", name,
						oldpackage.version, version,
						target->identifier);
				}
			} else if (versioncmp < 0) {
				/* new Version is older than
				 * old version */
				if (keep_old) {
					struct packagedata samepackage;
					r = target_getpackage(target, name, version, &samepackage);
					if (RET_WAS_ERROR(r)) {
						packagedata_free(&oldpackage);
						return r;
					} else if (RET_IS_OK(r)) {
						if (!overwrite_existing) {
							fprintf(stderr,
		"Skipping inclusion of '%s' '%s' in '%s', as this version already exists.\n",
								name, version,
								target->identifier);
							packagedata_free(&oldpackage);
							packagedata_free(&samepackage);
							return RET_NOTHING;
						} else {
							fprintf(stderr,
		"Warning: replacing '%s' version '%s' with equal version '%s' in '%s'!\n", name,
								samepackage.version, version,
								target->identifier);
						}
					} else { // r == RET_NOTHING
						replace = false;
					}
					packagedata_free(&samepackage);
				} else if (!downgrade) {
					fprintf(stderr,
"Skipping inclusion of '%s' '%s' in '%s', as it has already '%s'.\n",
						name, version,
						target->identifier,
						oldpackage.version);
					packagedata_free(&oldpackage);
					return RET_NOTHING;
				} else {
					fprintf(stderr,
"Warning: downgrading '%s' from '%s' to '%s' in '%s'!\n", name,
						oldpackage.version, version,
						target->identifier);
				}
			} else { // versioncmp > 0
				replace = !keep_old;
			}
		}
		if (replace) {
			r = target->getfilekeys(oldpackage.chunk, &oldfilekeys);
			ofk = &oldfilekeys;
			if (RET_WAS_ERROR(r)) {
				if (IGNORING(brokenold,
"Error parsing files belonging to installed version of %s!\n", name)) {
					ofk = NULL;
					oldsversion = oldsource = NULL;
				} else {
					packagedata_free(&oldpackage);
					return r;
				}
			} else if (trackingdata != NULL) {
				r = target->getsourceandversion(oldpackage.chunk,
						name, &oldsource, &oldsversion);
				if (RET_WAS_ERROR(r)) {
					strlist_done(ofk);
					if (IGNORING(brokenold,
"Error searching for source name of installed version of %s!\n", name)) {
						// TODO: free something of oldfilekeys?
						ofk = NULL;
						oldsversion = oldsource = NULL;
					} else {
						packagedata_free(&oldpackage);
						return r;
					}
				}
			} else {
				oldsversion = oldsource = NULL;
			}
		}
	}
	newcontrol = NULL;
	r = description_addpackage(target, name, control, oldpackage.chunk,
			description, &newcontrol);
	if (RET_IS_OK(r))
		control = newcontrol;
	if (!RET_WAS_ERROR(r)) {
		if (!replace) {
			packagedata_free(&oldpackage);
			ofk = NULL;
			oldsversion = NULL;
			oldsource = NULL;
		}
		r = addpackages(target, name, control,
			version, &oldpackage,
			filekeys, ofk,
			logger,
			trackingdata, architecture, oldsource, oldsversion,
			causingrule, suitefrom);
	}
	if (RET_IS_OK(r)) {
		target->wasmodified = true;
		if (trackingdata == NULL)
			target->staletracking = true;
	}
	free(newcontrol);
	packagedata_free(&oldpackage);

	return r;
}

retvalue target_checkaddpackage(struct target *target, const char *name, const char *version, bool tracking, bool permitnewerold) {
	struct strlist oldfilekeys, *ofk;
	char *oldcontrol, *oldsource, *oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = table_getrecord(target->packages, name, &oldcontrol);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
		oldpversion = NULL;
		oldcontrol = NULL;
	} else {
		int versioncmp;

		r = target->getversion(oldcontrol, &oldpversion);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error extracting version from old '%s' in '%s'. Database corrupted?\n", name, target->identifier);
			free(oldcontrol);
			return r;
		}
		assert (RET_IS_OK(r));

		r = dpkgversions_cmp(version, oldpversion, &versioncmp);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Parse error comparing version '%s' of '%s' with old version '%s' in '%s'\n.",
					version, name, oldpversion,
					target->identifier);
			free(oldpversion);
			free(oldcontrol);
			return r;
		}
		if (versioncmp <= 0) {
			r = RET_NOTHING;
			if (versioncmp < 0) {
				if (!permitnewerold) {
					fprintf(stderr,
"Error: trying to put version '%s' of '%s' in '%s',\n"
"while there already is the stricly newer '%s' in there.\n"
"(To ignore this error add Permit: older_version.)\n",
						version, name,
						target->identifier,
						oldpversion);
					r = RET_ERROR;
				} else if (verbose >= 0) {
					printf(
"Warning: trying to put version '%s' of '%s' in '%s',\n"
"while there already is '%s' in there.\n",
						version, name,
						target->identifier,
						oldpversion);
				}
			} else if (verbose > 2) {
					printf(
"Will not put '%s' in '%s', as already there with same version '%s'.\n",
						name, target->identifier,
						oldpversion);

			}
			free(oldpversion);
			free(oldcontrol);
			return r;
		}
		r = target->getfilekeys(oldcontrol, &oldfilekeys);
		ofk = &oldfilekeys;
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error extracting installed files from old '%s' in '%s'.\nDatabase corrupted?\n",
				name, target->identifier);
			free(oldcontrol);
			free(oldpversion);
			return r;
		}
		if (tracking) {
			r = target->getsourceandversion(oldcontrol,
					name, &oldsource, &oldsversion);
			if (RET_WAS_ERROR(r)) {
				fprintf(stderr,
"Error extracting source name and version from '%s' in '%s'. Database corrupted?\n",
						name, target->identifier);
				strlist_done(ofk);
				free(oldcontrol);
				free(oldpversion);
				return r;
			}
			/* TODO: check if tracking would succeed */
			free(oldsversion);
			free(oldsource);
		}
		strlist_done(ofk);
	}
	free(oldpversion);
	free(oldcontrol);
	return RET_OK;
}

retvalue target_rereference(struct target *target) {
	retvalue result, r;
	struct target_cursor iterator;
	const char *package;
	struct packagedata packagedata;

	if (verbose > 1) {
		if (verbose > 2)
			printf("Unlocking dependencies of %s...\n",
					target->identifier);
		else
			printf("Rereferencing %s...\n",
					target->identifier);
	}

	result = references_remove(target->identifier);
	if (verbose > 2)
		printf("Referencing %s...\n", target->identifier);

	r = target_openiterator(target, READONLY, &iterator);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	while (target_nextpackage(&iterator, &package, &packagedata)) {
		struct strlist filekeys;

		r = target->getfilekeys(packagedata.chunk, &filekeys);
		RET_UPDATE(result, r);
		if (!RET_IS_OK(r))
			continue;
		if (verbose > 10) {
			fprintf(stderr, "adding references to '%s' for '%s': ",
					target->identifier, package);
			(void)strlist_fprint(stderr, &filekeys);
			(void)putc('\n', stderr);
		}
		r = references_insert(target->identifier, &filekeys, NULL);
		strlist_done(&filekeys);
		RET_UPDATE(result, r);
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue package_referenceforsnapshot(UNUSED(struct distribution *di), struct target *target, const char *package, const struct packagedata *packagedata, void *data) {
	const char *identifier = data;
	struct strlist filekeys;
	retvalue r;

	r = target->getfilekeys(packagedata->chunk, &filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	if (verbose > 15) {
		fprintf(stderr, "adding references to '%s' for '%s': ",
				identifier, package);
		(void)strlist_fprint(stderr, &filekeys);
		(void)putc('\n', stderr);
	}
	r = references_add(identifier, &filekeys);
	strlist_done(&filekeys);
	return r;
}

retvalue package_check(UNUSED(struct distribution *di), struct target *target, const char *package, const struct packagedata *packagedata, UNUSED(void *pd)) {
	struct checksumsarray files;
	struct strlist expectedfilekeys;
	char *dummy, *version;
	retvalue result = RET_OK, r;
	architecture_t package_architecture;

	r = target->getversion(packagedata->chunk, &version);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction version number from package control info of '%s'!\n",
				package);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getarchitecture(packagedata->chunk, &package_architecture);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction architecture from package control info of '%s'!\n",
				package);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	/* check if the architecture matches the architecture where this
	 * package belongs to. */
	if (target->architecture != package_architecture &&
	    package_architecture != architecture_all) {
		fprintf(stderr,
"Wrong architecture '%s' of package '%s' in '%s'!\n",
				atoms_architectures[package_architecture],
				package, target->identifier);
		result = RET_ERROR;
	}
	r = target->getinstalldata(target, package, version,
			package_architecture, packagedata->chunk, &dummy,
			&expectedfilekeys, &files);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Error extracting information of package '%s'!\n",
				package);
		result = r;
	}
	free(version);
	if (RET_IS_OK(r)) {
		free(dummy);
		if (!strlist_subset(&expectedfilekeys, &files.names, NULL) ||
		    !strlist_subset(&expectedfilekeys, &files.names, NULL)) {
			(void)fprintf(stderr,
"Reparsing the package information of '%s' yields to the expectation to find:\n",
					package);
			(void)strlist_fprint(stderr, &expectedfilekeys);
			(void)fputs("but found:\n", stderr);
			(void)strlist_fprint(stderr, &files.names);
			(void)putc('\n', stderr);
			result = RET_ERROR;
		}
		strlist_done(&expectedfilekeys);
	} else {
		r = target->getchecksums(packagedata->chunk, &files);
		if (r == RET_NOTHING)
			r = RET_ERROR;
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Even more errors extracting information of package '%s'!\n",
					package);
			return r;
		}
	}

	if (verbose > 10) {
		fprintf(stderr, "checking files of '%s'\n", package);
	}
	r = files_expectfiles(&files.names, files.checksums);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr, "Files are missing for '%s'!\n", package);
	}
	RET_UPDATE(result, r);
	if (verbose > 10) {
		(void)fprintf(stderr, "checking references to '%s' for '%s': ",
				target->identifier, package);
		(void)strlist_fprint(stderr, &files.names);
		(void)putc('\n', stderr);
	}
	r = references_check(target->identifier, &files.names);
	RET_UPDATE(result, r);
	checksumsarray_done(&files);
	return result;
}

/* Reapply override information */

retvalue target_reoverride(struct target *target, struct distribution *distribution) {
	struct target_cursor iterator;
	retvalue result, r;
	const char *package;
	struct packagedata packagedata;

	assert(target->packages == NULL);
	assert(distribution != NULL);

	if (verbose > 1) {
		fprintf(stderr,
"Reapplying overrides packages in '%s'...\n",
				target->identifier);
	}

	r = target_openiterator(target, READWRITE, &iterator);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (target_nextpackage(&iterator, &package, &packagedata)) {
		char *newcontrolchunk = NULL;

		r = target->doreoverride(target, package, packagedata.chunk,
				&newcontrolchunk);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r)) {
			if (verbose > 0)
				(void)fputs(
"target_reoverride: Stopping procession of further packages due to previous errors\n",
						stderr);
			break;
		}
		if (RET_IS_OK(r)) {
			r = cursor_replace(target->packages, iterator.cursor,
				newcontrolchunk, strlen(newcontrolchunk));
			free(newcontrolchunk);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
			target->wasmodified = true;
		}
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

/* Readd checksum information */

static retvalue complete_package_checksums(struct target *target, const char *control, char **n) {
	struct checksumsarray files;
	retvalue r;

	r = target->getchecksums(control, &files);
	if (!RET_IS_OK(r))
		return r;

	r = files_checkorimprove(&files.names, files.checksums);
	if (!RET_IS_OK(r)) {
		checksumsarray_done(&files);
		return r;
	}
	r = target->completechecksums(control,
			&files.names, files.checksums, n);
	checksumsarray_done(&files);
	return r;
}

retvalue target_redochecksums(struct target *target, struct distribution *distribution) {
	struct target_cursor iterator;
	retvalue result, r;
	const char *package;
	struct packagedata packagedata;

	assert(target->packages == NULL);
	assert(distribution != NULL);

	if (verbose > 1) {
		fprintf(stderr,
"Redoing checksum information for packages in '%s'...\n",
				target->identifier);
	}

	r = target_openiterator(target, READWRITE, &iterator);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (target_nextpackage(&iterator, &package, &packagedata)) {
		char *newcontrolchunk = NULL;

		r = complete_package_checksums(target, packagedata.chunk,
				&newcontrolchunk);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (RET_IS_OK(r)) {
			r = cursor_replace(target->packages, iterator.cursor,
				newcontrolchunk, strlen(newcontrolchunk));
			free(newcontrolchunk);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
			target->wasmodified = true;
		}
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

/* export a database */

retvalue target_export(struct target *target, bool onlyneeded, bool snapshot, struct release *release) {
	retvalue result;
	bool onlymissing;

	assert (!target->noexport);

	if (verbose > 5) {
		if (onlyneeded)
			printf(" looking for changes in '%s'...\n",
					target->identifier);
		else
			printf(" exporting '%s'...\n", target->identifier);
	}

	/* not exporting if file is already there? */
	onlymissing = onlyneeded && !target->wasmodified;

	result = export_target(target->relativedirectory, target,
			target->exportmode, release, onlymissing, snapshot);

	if (!RET_WAS_ERROR(result) && !snapshot) {
		target->saved_wasmodified =
			target->saved_wasmodified || target->wasmodified;
		target->wasmodified = false;
	}
	return result;
}

retvalue package_rerunnotifiers(struct distribution *distribution, struct target *target, const char *package, const struct packagedata *packagedata, UNUSED(void *data)) {
	struct logger *logger = distribution->logger;
	struct strlist filekeys;
	char *version;
	retvalue r;

	r = target->getversion(packagedata->chunk, &version);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction version number from package control info of '%s'!\n",
				package);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getfilekeys(packagedata->chunk, &filekeys);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Error extracting information about used files from package '%s'!\n",
				package);
		free(version);
		return r;
	}
	r = logger_reruninfo(logger, target, package, version, packagedata->chunk, &filekeys);
	strlist_done(&filekeys);
	free(version);
	return r;
}
