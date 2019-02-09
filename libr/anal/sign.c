/* radare - LGPL - Copyright 2009-2019 - pancake, nibble */

#include <r_anal.h>
#include <r_sign.h>
#include <r_search.h>
#include <r_util.h>
#include <r_core.h>
#include <r_hash.h>

R_LIB_VERSION (r_sign);

const char *getRealRef(RCore *core, ut64 off) {
	RFlagItem *item;
	RListIter *iter;

	const RList *list = r_flag_get_list (core->flags, off);
	if (!list) {
		return NULL;
	}

	r_list_foreach (list, iter, item) {
		if (!item->name) {
			continue;
		}
		if (strncmp (item->name, "sym.", 4)) {
			continue;
		}
		return item->name;
	}

	return NULL;
}

R_API RList *r_sign_fcn_vars(RAnal *a, RAnalFunction *fcn) {
	r_return_val_if_fail (a && fcn, NULL);

	RCore *core = a->coreb.core;

	if (!core) {
		return NULL;
	}

	RListIter *iter;
	RAnalVar *var;
	RList *ret = r_list_newf ((RListFree) free);
	if (!ret) {
		return NULL;
	}
        RList *reg_vars = r_anal_var_list (core->anal, fcn, R_ANAL_VAR_KIND_REG);
        RList *spv_vars = r_anal_var_list (core->anal, fcn, R_ANAL_VAR_KIND_SPV);
        RList *bpv_vars = r_anal_var_list (core->anal, fcn, R_ANAL_VAR_KIND_BPV);
	r_list_foreach (bpv_vars, iter, var) {
		r_list_append (ret, r_str_newf ("b%d", var->delta));
	}
	r_list_foreach (spv_vars, iter, var) {
		r_list_append (ret, r_str_newf ("s%d", var->delta));
	}
	r_list_foreach (reg_vars, iter, var) {
		r_list_append (ret, r_str_newf ("r%d", var->delta));
	}
	r_list_free (reg_vars);
	r_list_free (bpv_vars);
	r_list_free (spv_vars);
	return ret;
}

R_API RList *r_sign_fcn_refs(RAnal *a, RAnalFunction *fcn) {
	RListIter *iter = NULL;
	RAnalRef *refi = NULL;

	r_return_val_if_fail (a && fcn, NULL);

	RCore *core = a->coreb.core;

	if (!core) {
		return NULL;
	}

	RList *ret = r_list_newf ((RListFree) free);
	RList *refs = r_anal_fcn_get_refs (a, fcn);
	r_list_foreach (refs, iter, refi) {
		if (refi->type == R_ANAL_REF_TYPE_CODE || refi->type == R_ANAL_REF_TYPE_CALL) {
			const char *flag = getRealRef (core, refi->addr);
			if (flag) {
				r_list_append (ret, r_str_newf (flag));
			}
		}
	}
	r_list_free (refs);
	return ret;
}

static bool deserialize(RAnal *a, RSignItem *it, const char *k, const char *v) {
	char *refs = NULL;
	char *vars = NULL;
	const char *token = NULL;
	int i = 0, n = 0, nrefs = 0, nvars = 0, size = 0;
	bool retval = true;

	char *k2 = r_str_new (k);
	char *v2 = r_str_new (v);
	if (!k2 || !v2) {
		free (k2);
		free (v2);
		return false;
	}

	// Deserialize key: zign|space|name
	n = r_str_split (k2, '|');
	if (n != 3) {
		retval = false;
		goto out;
	}

	// space (1)
	it->space = r_spaces_add (&a->zign_spaces, r_str_word_get0 (k2, 1));

	// name (2)
	it->name = r_str_new (r_str_word_get0 (k2, 2));

	// Deserialize val: size|bytes|mask|graph|addr|refs|bbhash
	n = r_str_split (v2, '|');
	if (n != 8) {
		retval = false;
		goto out;
	}

	// pattern size (0)
	size = atoi (r_str_word_get0 (v2, 0));
	if (size > 0) {
		it->bytes = R_NEW0 (RSignBytes);
		if (!it->bytes) {
			retval = false;
			goto out;
		}
		it->bytes->size = size;

		// bytes (1)
		token = r_str_word_get0 (v2, 1);
		if (strlen (token) != 2 * it->bytes->size) {
			retval = false;
			goto out;
		}
		it->bytes->bytes = malloc (it->bytes->size);
		if (!it->bytes->bytes) {
			retval = false;
			goto out;
		}
		r_hex_str2bin (token, it->bytes->bytes);

		// mask (2)
		token = r_str_word_get0 (v2, 2);
		if (strlen (token) != 2 * it->bytes->size) {
			retval = false;
			goto out;
		}
		it->bytes->mask = malloc (it->bytes->size);
		if (!it->bytes->mask) {
			retval = false;
			goto out;
		}
		r_hex_str2bin (token, it->bytes->mask);
	}

	// graph metrics (3)
	token = r_str_word_get0 (v2, 3);
	if (strlen (token) == 2 * sizeof (RSignGraph)) {
		it->graph = R_NEW0 (RSignGraph);
		if (it->graph) {
			r_hex_str2bin (token, (ut8 *) it->graph);
		}
	}

	// addr (4)
	token = r_str_word_get0 (v2, 4);
	it->addr = atoll (token);

	// refs (5)
	token = r_str_word_get0 (v2, 5);
	refs = r_str_new (token);
	nrefs = r_str_split (refs, ',');
	if (nrefs > 0) {
		it->refs = r_list_newf ((RListFree) free);
		for (i = 0; i < nrefs; i++) {
			r_list_append (it->refs, r_str_newf (r_str_word_get0 (refs, i)));
		}
	}

	// vars (6)
	token = r_str_word_get0 (v2, 6);
	vars = r_str_new (token);
	nvars = r_str_split (vars, ',');
	if (nvars > 0) {
		it->vars = r_list_newf ((RListFree) free);
		for (i = 0; i < nvars; i++) {
			r_list_append (it->vars, r_str_newf (r_str_word_get0 (vars, i)));
		}
	}

	// basic blocks hash (7)
	token = r_str_word_get0 (v2, 7);
	if (token[0] != 0) {
		it->hash = R_NEW0 (RSignHash);
		if (!it->hash) {
			goto out;
		}
		it->hash->bbhash = r_str_new (token);
	}
out:
	free (k2);
	free (v2);
	free (refs);
	free (vars);

	return retval;
}

static void serializeKey(RAnal *a, const RSpace *space, const char* name, char *k) {
	snprintf (k, R_SIGN_KEY_MAXSZ, "zign|%s|%s",
		space? space->name: "*", name);
}

static void serializeKeySpaceStr(RAnal *a, const char *space, const char* name, char *k) {
	snprintf (k, R_SIGN_KEY_MAXSZ, "zign|%s|%s", space, name);
}

static void serialize(RAnal *a, RSignItem *it, char *k, char *v) {
	RListIter *iter = NULL;
	char *hexbytes = NULL, *hexmask = NULL, *hexgraph = NULL;
	char *refs = NULL, *ref = NULL, *var, *vars = NULL;
	int i = 0, len = 0;
	RSignBytes *bytes = it->bytes;
	RSignGraph *graph = it->graph;
	RSignHash *hash = it->hash;

	if (k) {
		serializeKey (a, it->space, it->name, k);
	}

	if (v) {
		if (bytes) {
			len = bytes->size * 2 + 1;
			hexbytes = calloc (1, len);
			hexmask = calloc (1, len);
			if (!hexbytes || !hexmask) {
				free (hexbytes);
				free (hexmask);
				return;
			}
			r_hex_bin2str (bytes->bytes, bytes->size, hexbytes);
			r_hex_bin2str (bytes->mask, bytes->size, hexmask);
		}
		if (graph) {
			hexgraph = calloc (1, sizeof (RSignGraph) * 2 + 1);
			if (hexgraph) {
				r_hex_bin2str ((ut8 *) graph, sizeof (RSignGraph), hexgraph);
			}
		}
		i = 0;
		r_list_foreach (it->refs, iter, ref) {
			if (i > 0) {
				refs = r_str_appendch (refs, ',');
			}
			refs = r_str_append (refs, ref);
			i++;
		}
		i = 0;
		r_list_foreach (it->vars, iter, var) {
			if (i > 0) {
				vars = r_str_appendch (vars, ',');
			}
			vars = r_str_append (vars, var);
			i++;
		}
		snprintf (v, R_SIGN_VAL_MAXSZ, "%d|%s|%s|%s|%"PFMT64d"|%s|%s|%s",
			bytes? bytes->size: 0,
			bytes? hexbytes: "",
			bytes? hexmask: "",
			graph? hexgraph: "",
			it->addr,
			refs? refs: "",
			vars? vars: "",
			hash? hash->bbhash: "");

		free (hexbytes);
		free (hexmask);
		free (hexgraph);
		free (refs);
		free (vars);
	}
}

static void mergeItem(RSignItem *dst, RSignItem *src) {
	RListIter *iter = NULL;
	char *ref, *var;

	if (src->bytes) {
		if (dst->bytes) {
			free (dst->bytes->bytes);
			free (dst->bytes->mask);
			free (dst->bytes);
		}
		dst->bytes = R_NEW0 (RSignBytes);
		if (!dst->bytes) {
			return;
		}
		dst->bytes->size = src->bytes->size;
		dst->bytes->bytes = malloc (src->bytes->size);
		if (!dst->bytes->bytes) {
			free (dst->bytes);
			return;
		}
		memcpy (dst->bytes->bytes, src->bytes->bytes, src->bytes->size);
		dst->bytes->mask = malloc (src->bytes->size);
		if (!dst->bytes->mask) {
			free (dst->bytes->bytes);
			free (dst->bytes);
			return;
		}
		memcpy (dst->bytes->mask, src->bytes->mask, src->bytes->size);
	}

	if (src->graph) {
		free (dst->graph);
		dst->graph = R_NEW0 (RSignGraph);
		if (!dst->graph) {
			return;
		}
		*dst->graph = *src->graph;
	}

	if (src->addr != UT64_MAX) {
		dst->addr = src->addr;
	}

	if (src->refs) {
		r_list_free (dst->refs);

		dst->refs = r_list_newf ((RListFree) free);
		r_list_foreach (src->refs, iter, ref) {
			r_list_append (dst->refs, r_str_new (ref));
		}
	}

	if (src->vars) {
		r_list_free (dst->vars);

		dst->vars = r_list_newf ((RListFree) free);
		r_list_foreach (src->vars, iter, var) {
			r_list_append (dst->vars, r_str_new (var));
		}
	}

	if (src->hash) {
		if (!dst->hash) {
			dst->hash = R_NEW0 (RSignHash);
		}
			if (!dst->hash) {
				return;
			}
		if (src->hash->bbhash) {
			dst->hash->bbhash = strdup (src->hash->bbhash);
		}
	}
}

static bool addItem(RAnal *a, RSignItem *it) {
	char key[R_SIGN_KEY_MAXSZ], val[R_SIGN_VAL_MAXSZ];
	const char *curval = NULL;
	bool retval = true;
	RSignItem *curit = r_sign_item_new ();
	if (!curit) {
		return false;
	}

	serialize (a, it, key, val);
	curval = sdb_const_get (a->sdb_zigns, key, 0);
	if (curval) {
		if (!deserialize (a, curit, key, curval)) {
			eprintf ("error: cannot deserialize zign\n");
			retval = false;
			goto out;
		}
		mergeItem (curit, it);
		serialize (a, curit, key, val);
	}
	sdb_set (a->sdb_zigns, key, val, 0);

out:
	r_sign_item_free (curit);

	return retval;
}

static bool addHash(RAnal *a, const char *name, int type, const char *val) {
	RSignItem *it = r_sign_item_new ();
	if (!it) {
		r_sign_item_free (it);
		return false;
	}
	it->name = r_str_new (name);
	if (!it->name) {
		r_sign_item_free (it);
		return false;
	}
	it->hash = R_NEW0 (RSignHash);
	if (!it->hash) {
		r_sign_item_free (it);
		return false;
	}

	switch (type) {
	case R_SIGN_BBHASH:
		it->hash->bbhash = strdup(val);
		break;
	}

	bool retval = addItem (a, it);
	r_sign_item_free (it);
	return retval;
}

static bool addBBHash(RAnal *a, RAnalFunction *fcn, const char *name) {
	bool retval = false;
	RSignItem *it = r_sign_item_new ();
	if (!it) {
		goto beach;
	}
	it->name = r_str_new (name);
	if (!it->name) {
		goto beach;
	}
	it->hash = R_NEW0 (RSignHash);
	if (!it->hash) {
		goto beach;
	}

	char *digest_hex = r_sign_calc_bbhash (a, fcn);
	if (!digest_hex) {
		free (digest_hex);
		goto beach;
	}
	it->hash->bbhash = digest_hex;
	retval = addItem (a, it);
beach:
	r_sign_item_free (it);
	return retval;
}

static bool addBytes(RAnal *a, const char *name, ut64 size, const ut8 *bytes, const ut8 *mask) {
	bool retval = true;

	if (r_mem_is_zero (mask, size)) {
		eprintf ("error: zero mask\n");
		return false;
	}

	RSignItem *it = r_sign_item_new ();
	if (!it) {
		return false;
	}

	it->name = r_str_new (name);
	if (!it->name) {
		free (it);
		return false;
	}
	it->space = r_spaces_current (&a->zign_spaces);
	it->bytes = R_NEW0 (RSignBytes);
	if (!it->bytes) {
		goto fail;
	}
	it->bytes->size = size;
	it->bytes->bytes = malloc (size);
	if (!it->bytes->bytes) {
		goto fail;
	}
	memcpy (it->bytes->bytes, bytes, size);
	it->bytes->mask = malloc (size);
	if (!it->bytes->mask) {
		goto fail;
	}
	memcpy (it->bytes->mask, mask, size);
	retval = addItem (a, it);
	r_sign_item_free (it);
	return retval;
fail:
	if (it) {
		free (it->name);
		if (it->bytes) {
			free (it->bytes->bytes);
			free (it->bytes);
		}
	}
	free (it);
	return false;
}

R_API bool r_sign_add_hash(RAnal *a, const char *name, int type, const char *val, int len) {
	r_return_val_if_fail (a && name && type && val && len > 0, false);
	if (type != R_SIGN_BBHASH) {
		eprintf ("error: hash type unknown");
	}
	int digestsize = r_hash_size (R_ZIGN_HASH) * 2;
	if (len != digestsize) {
		eprintf ("error: invalid hash size: %d (%s digest size is %d)\n", len, ZIGN_HASH, digestsize);
		return false;
	}
	return addHash (a, name, type, val);
}

R_API bool r_sign_add_bb_hash(RAnal *a, RAnalFunction *fcn, const char *name) {
	r_return_val_if_fail (a && fcn && name, false);
	return addBBHash (a, fcn, name);
}

R_API bool r_sign_add_bytes(RAnal *a, const char *name, ut64 size, const ut8 *bytes, const ut8 *mask) {
	r_return_val_if_fail (a && name && size > 0 && bytes && mask, false);

	return addBytes (a, name, size, bytes, mask);
}

R_API bool r_sign_add_anal(RAnal *a, const char *name, ut64 size, const ut8 *bytes, ut64 at) {
	ut8 *mask = NULL;
	bool retval = true;

	r_return_val_if_fail (a && name && size > 0 && bytes, false);

	mask = r_anal_mask (a, size, bytes, at);
	if (!mask) {
		return false;
	}

	retval = addBytes (a, name, size, bytes, mask);

	free (mask);
	return retval;
}

R_API bool r_sign_add_graph(RAnal *a, const char *name, RSignGraph graph) {
	bool retval = true;
	if (!a || !name) {
		return false;
	}
	RSignItem *it = r_sign_item_new ();
	if (!it) {
		return false;
	}
	it->name = r_str_new (name);
	if (!it->name) {
		free (it);
		return false;
	}
	it->space = r_spaces_current (&a->zign_spaces);
	it->graph = R_NEW0 (RSignGraph);
	if (!it->graph) {
		free (it->name);
		free (it);
		return false;
	}
	*it->graph = graph;
	retval = addItem (a, it);
	r_sign_item_free (it);

	return retval;
}

R_API bool r_sign_add_addr(RAnal *a, const char *name, ut64 addr) {
	r_return_val_if_fail (a && name && addr != UT64_MAX, false);

	RSignItem *it = r_sign_item_new ();
	if (!it) {
		return NULL;
	}
	it->name = r_str_new (name);
	it->space = r_spaces_current (&a->zign_spaces);
	it->addr = addr;

	bool retval = addItem (a, it);

	r_sign_item_free (it);

	return retval;
}

R_API bool r_sign_add_vars(RAnal *a, const char *name, RList *vars) {
	r_return_val_if_fail (a && name && vars, false);

	RListIter *iter;
	char *var;

	RSignItem *it = r_sign_item_new ();
	if (!it) {
		return false;
	}
	it->name = r_str_new (name);
	if (!it->name) {
		r_sign_item_free (it);
		return false;
	}
	it->space = r_spaces_current (&a->zign_spaces);
	it->vars = r_list_newf ((RListFree) free);
	r_list_foreach (vars, iter, var) {
		r_list_append (it->vars, strdup (var));
	}
	bool retval = addItem (a, it);
	r_sign_item_free (it);

	return retval;
}

R_API bool r_sign_add_refs(RAnal *a, const char *name, RList *refs) {
	r_return_val_if_fail (a && name && refs, false);

	RListIter *iter = NULL;
	char *ref = NULL;
	RSignItem *it = r_sign_item_new ();
	if (!it) {
		return false;
	}
	it->name = r_str_new (name);
	if (!it->name) {
		free (it);
		return false;
	}
	it->space = r_spaces_current (&a->zign_spaces);
	it->refs = r_list_newf ((RListFree) free);
	r_list_foreach (refs, iter, ref) {
		r_list_append (it->refs, strdup (ref));
	}
	bool retval = addItem (a, it);
	r_sign_item_free (it);

	return retval;
}

struct ctxDeleteCB {
	RAnal *anal;
	char buf[R_SIGN_KEY_MAXSZ];
};

static int deleteBySpaceCB(void *user, const char *k, const char *v) {
	struct ctxDeleteCB *ctx = (struct ctxDeleteCB *) user;

	if (!strncmp (k, ctx->buf, strlen (ctx->buf))) {
		sdb_remove (ctx->anal->sdb_zigns, k, 0);
	}

	return 1;
}

R_API bool r_sign_delete(RAnal *a, const char *name) {
	struct ctxDeleteCB ctx = {0};
	char k[R_SIGN_KEY_MAXSZ];

	if (!a || !name) {
		return false;
	}
	// Remove all zigns
	if (*name == '*') {
		if (!r_spaces_current (&a->zign_spaces)) {
			sdb_reset (a->sdb_zigns);
			return true;
		}
		ctx.anal = a;
		serializeKey (a, r_spaces_current (&a->zign_spaces), "", ctx.buf);
		sdb_foreach (a->sdb_zigns, deleteBySpaceCB, &ctx);
		return true;
	}
	// Remove specific zign
	serializeKey (a, r_spaces_current (&a->zign_spaces), name, k);
	return sdb_remove (a->sdb_zigns, k, 0);
}

struct ctxListCB {
	RAnal *anal;
	int idx;
	int format;
};

struct ctxGetListCB {
	RAnal *anal;
	RList *list;
};

static void listBytes(RAnal *a, RSignItem *it, int format) {
	RSignBytes *bytes = it->bytes;
	char *strbytes = NULL;
	int i = 0;

	for (i = 0; i < bytes->size; i++) {
		if (bytes->mask[i] & 0xf0) {
			strbytes = r_str_appendf (strbytes, "%x", (bytes->bytes[i] & 0xf0) >> 4);
		} else {
			strbytes = r_str_appendf (strbytes, ".");
		}
		if (bytes->mask[i] & 0xf) {
			strbytes = r_str_appendf (strbytes, "%x", bytes->bytes[i] & 0xf);
		} else {
			strbytes = r_str_appendf (strbytes, ".");
		}
	}

	if (strbytes) {
		if (format == '*') {
			a->cb_printf ("za %s b %s\n", it->name, strbytes);
		} else if (format == 'j') {
			a->cb_printf ("\"bytes\":\"%s\",", strbytes);
		} else {
			a->cb_printf ("  bytes: %s\n", strbytes);
		}
		free (strbytes);
	}
}

static void listGraph(RAnal *a, RSignItem *it, int format) {
	RSignGraph *graph = it->graph;

	if (format == '*') {
		a->cb_printf ("za %s g cc=%d nbbs=%d edges=%d ebbs=%d bbsum=%d\n",
			it->name, graph->cc, graph->nbbs, graph->edges, graph->ebbs, graph->bbsum);
	} else if (format == 'j') {
		a->cb_printf ("\"graph\":{\"cc\":%d,\"nbbs\":%d,\"edges\":%d,\"ebbs\":%d,\"bbsum\":%d},",
			graph->cc, graph->nbbs, graph->edges, graph->ebbs, graph->bbsum);
	} else {
		a->cb_printf ("  graph: cc=%d nbbs=%d edges=%d ebbs=%d bbsum=%d\n",
			graph->cc, graph->nbbs, graph->edges, graph->ebbs, graph->bbsum);
	}
}

static void listOffset(RAnal *a, RSignItem *it, int format) {
	if (format == '*') {
		a->cb_printf ("za %s o 0x%08"PFMT64x"\n", it->name, it->addr);
	} else if (format == 'j') {
		a->cb_printf ("\"addr\":%"PFMT64d",", it->addr);
	} else {
		a->cb_printf ("  addr: 0x%08"PFMT64x"\n", it->addr);
	}
}

static void listVars(RAnal *a, RSignItem *it, int format) {
	RListIter *iter = NULL;
	char *var = NULL;
	int i = 0;

	if (format == '*') {
		a->cb_printf ("za %s v ", it->name);
	} else if (format == 'j') {
		a->cb_printf ("\"vars\":[");
	} else {
		a->cb_printf ("  vars: ");
	}

	r_list_foreach (it->vars, iter, var) {
		if (i > 0) {
			if (format == '*') {
				a->cb_printf (" ");
			} else if (format == 'j') {
				a->cb_printf (",");
			} else {
				a->cb_printf (", ");
			}
		}
		if (format == 'j') {
			a->cb_printf ("\"%s\"", var);
		} else {
			a->cb_printf ("%s", var);
		}
		i++;
	}

	if (format == 'j') {
		a->cb_printf ("],");
	} else {
		a->cb_printf ("\n");
	}
}

static void listRefs(RAnal *a, RSignItem *it, int format) {
	RListIter *iter = NULL;
	char *ref = NULL;
	int i = 0;

	if (format == '*') {
		a->cb_printf ("za %s r ", it->name);
	} else if (format == 'j') {
		a->cb_printf ("\"refs\":[");
	} else {
		a->cb_printf ("  refs: ");
	}

	r_list_foreach (it->refs, iter, ref) {
		if (i > 0) {
			if (format == '*') {
				a->cb_printf (" ");
			} else if (format == 'j') {
				a->cb_printf (",");
			} else {
				a->cb_printf (", ");
			}
		}
		if (format == 'j') {
			a->cb_printf ("\"%s\"", ref);
		} else {
			a->cb_printf ("%s", ref);
		}
		i++;
	}

	if (format == 'j') {
		a->cb_printf ("],");
	} else {
		a->cb_printf ("\n");
	}
}

static void listHash(RAnal *a, RSignItem *it, int format) {
	if (it->hash) {
		if (format == '*') {
			if (it->hash->bbhash) {
				a->cb_printf ("za %s h %s\n", it->name, it->hash->bbhash);
			}
		} else if (format == 'j') {
			a->cb_printf ("\"hash\":{");
			if (it->hash->bbhash) {
				a->cb_printf ("\"bbhash\":\"%s\"", it->hash->bbhash);
			}
			a->cb_printf ("}");
		} else {
			if (it->hash->bbhash) {
				a->cb_printf ("  bbhash: %s\n", it->hash->bbhash);
			}
		}
	}
}

static int listCB(void *user, const char *k, const char *v) {
	struct ctxListCB *ctx = (struct ctxListCB *) user;
	RSignItem *it = r_sign_item_new ();
	RAnal *a = ctx->anal;

	if (!deserialize (a, it, k, v)) {
		eprintf ("error: cannot deserialize zign\n");
		goto out;
	}

	RSpace *cur = r_spaces_current (&a->zign_spaces);
	if (cur != it->space && cur) {
		goto out;
	}

	// Start item
	if (ctx->format == 'j') {
		if (ctx->idx > 0) {
			a->cb_printf (",");
		}
		a->cb_printf ("{");
	}

	// Zignspace and name (except for radare format)
	if (ctx->format == '*') {
		if (it->space) {
			a->cb_printf ("zs %s\n", it->space->name);
		} else {
			a->cb_printf ("zs *\n");
		}
	} else if (ctx->format == 'j') {
		if (it->space) {
			a->cb_printf ("{\"zignspace\":\"%s\",", it->space->name);
		}
		a->cb_printf ("\"name\":\"%s\",", it->name);
	} else {
		if (!r_spaces_current (&a->zign_spaces) && it->space) {
			a->cb_printf ("(%s) ", it->space->name);
		}
		a->cb_printf ("%s:\n", it->name);
	}

	// Bytes pattern
	if (it->bytes) {
		listBytes (a, it, ctx->format);
	} else if (ctx->format == 'j') {
		a->cb_printf ("\"bytes\":\"\",");
	}

	// Graph metrics
	if (it->graph) {
		listGraph (a, it, ctx->format);
	} else if (ctx->format == 'j') {
		a->cb_printf ("\"graph\":{},");
	}

	// Offset
	if (it->addr != UT64_MAX) {
		listOffset (a, it, ctx->format);
	} else if (ctx->format == 'j') {
		a->cb_printf ("\"addr\":-1,");
	}

	// References
	if (it->refs) {
		listRefs (a, it, ctx->format);
	} else if (ctx->format == 'j') {
		a->cb_printf ("\"refs\":[],");
	}

	// Vars
	if (it->vars) {
		listVars (a, it, ctx->format);
	} else if (ctx->format == 'j') {
		a->cb_printf ("\"vars\":[],");
	}

	// Hash
	if (it->hash) {
		listHash (a, it, ctx->format);
	} else if (ctx->format == 'j') {
		a->cb_printf ("\"hash\":{}");
	}

	// End item
	if (ctx->format == 'j') {
		a->cb_printf ("}");
	}

	ctx->idx++;

out:
	r_sign_item_free (it);

	return 1;
}

R_API void r_sign_list(RAnal *a, int format) {
	r_return_if_fail (a);
	struct ctxListCB ctx = { a, 0, format };

	if (format == 'j') {
		a->cb_printf ("[");
	}

	sdb_foreach (a->sdb_zigns, listCB, &ctx);

	if (format == 'j') {
		a->cb_printf ("]\n");
	}
}

static int listGetCB(void *user, const char *key, const char *val) {
	struct ctxGetListCB *ctx = user;
	RSignItem *item = r_sign_item_new ();
	if (!item) {
		return false;
	}
	if (!deserialize (ctx->anal, item, key, val)) {
		r_sign_item_free (item);
		return false;
	}
	r_list_append (ctx->list, item);

	return 1;
}

R_API RList *r_sign_get_list(RAnal *a) {
	r_return_val_if_fail (a, NULL);
	struct ctxGetListCB ctx = { a, r_list_newf ((RListFree)r_sign_item_free) };

	sdb_foreach (a->sdb_zigns, listGetCB, &ctx);

	return ctx.list;
}

static int cmpaddr(const void *_a, const void *_b) {
	const RAnalBlock *a = _a, *b = _b;
	return (a->addr - b->addr);
}

R_API char *r_sign_calc_bbhash(RAnal *a, RAnalFunction *fcn) {
	RListIter *iter = NULL;
	RAnalBlock *bbi = NULL;
	char *digest_hex = NULL;
	RHash *ctx = r_hash_new (true, R_ZIGN_HASH);
	if (!ctx) {
		goto beach;
	}
	r_list_sort (fcn->bbs, &cmpaddr);
	r_hash_do_begin (ctx, R_ZIGN_HASH);
	r_list_foreach (fcn->bbs, iter, bbi) {
		ut8 *buf = malloc (bbi->size);
		if (!buf) {
			goto beach;
		}
		if (!a->iob.read_at (a->iob.io, bbi->addr, buf, bbi->size)) {
			goto beach;
		}
		if (!r_hash_do_sha256 (ctx, buf, bbi->size)) {
			goto beach;
		}
		free (buf);
	}
	r_hash_do_end (ctx, R_ZIGN_HASH);

	digest_hex = r_hex_bin2strdup (ctx->digest, r_hash_size (R_ZIGN_HASH));
beach:
	free (ctx);
	return digest_hex;
}

struct ctxCountForCB {
	RAnal *anal;
	const RSpace *space;
	int count;
};

static int countForCB(void *user, const char *k, const char *v) {
	struct ctxCountForCB *ctx = (struct ctxCountForCB *) user;
	RSignItem *it = r_sign_item_new ();

	if (!deserialize (ctx->anal, it, k, v)) {
		eprintf ("error: cannot deserialize zign\n");
		goto out;
	}

	if (it->space == ctx->space) {
		ctx->count++;
	}

out:
	r_sign_item_free (it);

	return 1;
}

R_API int r_sign_space_count_for(RAnal *a, const RSpace *space) {
	struct ctxCountForCB ctx = { a, space, 0 };

	if (!a) {
		return 0;
	}

	sdb_foreach (a->sdb_zigns, countForCB, &ctx);

	return ctx.count;
}

struct ctxUnsetForCB {
	RAnal *anal;
	const RSpace *space;
};

static int unsetForCB(void *user, const char *k, const char *v) {
	struct ctxUnsetForCB *ctx = (struct ctxUnsetForCB *) user;
	char nk[R_SIGN_KEY_MAXSZ], nv[R_SIGN_VAL_MAXSZ];
	RSignItem *it = r_sign_item_new ();
	Sdb *db = ctx->anal->sdb_zigns;
	RAnal *a = ctx->anal;

	if (!deserialize (a, it, k, v)) {
		eprintf ("error: cannot deserialize zign\n");
		goto out;
	}

	if (it->space != ctx->space) {
		goto out;
	}

	if (it->space) {
		it->space = NULL;
		serialize (a, it, nk, nv);
		sdb_remove (db, k, 0);
		sdb_set (db, nk, nv, 0);
	}

out:
	r_sign_item_free (it);

	return 1;
}

R_API void r_sign_space_unset_for(RAnal *a, const RSpace *space) {
	struct ctxUnsetForCB ctx = { a, space };

	if (!a) {
		return;
	}

	sdb_foreach (a->sdb_zigns, unsetForCB, &ctx);
}

struct ctxRenameForCB {
	RAnal *anal;
	char oprefix[R_SIGN_KEY_MAXSZ];
	char nprefix[R_SIGN_KEY_MAXSZ];
};

static int renameForCB(void *user, const char *k, const char *v) {
	struct ctxRenameForCB *ctx = (struct ctxRenameForCB *) user;
	char nk[R_SIGN_KEY_MAXSZ], nv[R_SIGN_VAL_MAXSZ];
	const char *zigname = NULL;
	Sdb *db = ctx->anal->sdb_zigns;

	if (!strncmp (k, ctx->oprefix, strlen (ctx->oprefix))) {
		zigname = k + strlen (ctx->oprefix);
		snprintf (nk, R_SIGN_KEY_MAXSZ, "%s%s", ctx->nprefix, zigname);
		snprintf (nv, R_SIGN_VAL_MAXSZ, "%s", v);
		sdb_remove (db, k, 0);
		sdb_set (db, nk, nv, 0);
	}

	return 1;
}

R_API void r_sign_space_rename_for(RAnal *a, const RSpace *space, const char *oname, const char *nname) {
	struct ctxRenameForCB ctx;

	if (!a || !oname || !nname) {
		return;
	}

	ctx.anal = a;
	serializeKeySpaceStr (a, oname, "", ctx.oprefix);
	serializeKeySpaceStr (a, nname, "", ctx.nprefix);

	sdb_foreach (a->sdb_zigns, renameForCB, &ctx);
}

struct ctxForeachCB {
	RAnal *anal;
	RSignForeachCallback cb;
	void *user;
};

static int foreachCB(void *user, const char *k, const char *v) {
	struct ctxForeachCB *ctx = (struct ctxForeachCB *) user;
	RSignItem *it = r_sign_item_new ();
	RAnal *a = ctx->anal;
	int retval = 1;

	if (!deserialize (a, it, k, v)) {
		eprintf ("error: cannot deserialize zign\n");
		goto out;
	}

	RSpace *cur = r_spaces_current (&a->zign_spaces);
	if (cur != it->space && cur) {
		goto out;
	}

	if (ctx->cb) {
		retval = ctx->cb (it, ctx->user);
	}

out:
	r_sign_item_free (it);

	return retval;
}

R_API bool r_sign_foreach(RAnal *a, RSignForeachCallback cb, void *user) {
	struct ctxForeachCB ctx = { a, cb, user };

	if (!a || !cb) {
		return false;
	}

	return sdb_foreach (a->sdb_zigns, foreachCB, &ctx);
}

R_API RSignSearch *r_sign_search_new() {
	RSignSearch *ret = R_NEW0 (RSignSearch);

	ret->search = r_search_new (R_SEARCH_KEYWORD);
	ret->items = r_list_newf ((RListFree) r_sign_item_free);

	return ret;
}

R_API void r_sign_search_free(RSignSearch *ss) {
	if (!ss) {
		return;
	}

	r_search_free (ss->search);
	r_list_free (ss->items);
	free (ss);
}

static int searchHitCB(RSearchKeyword *kw, void *user, ut64 addr) {
	RSignSearch *ss = (RSignSearch *) user;

	if (ss->cb) {
		return ss->cb ((RSignItem *) kw->data, kw, addr, ss->user);
	}

	return 1;
}

struct ctxAddSearchKwCB {
	RSignSearch *ss;
	int minsz;
};

static int addSearchKwCB(RSignItem *it, void *user) {
	struct ctxAddSearchKwCB *ctx = (struct ctxAddSearchKwCB *) user;
	RSignSearch *ss = ctx->ss;
	RSignBytes *bytes = it->bytes;
	RSearchKeyword *kw = NULL;
	RSignItem *it2 = NULL;

	if (!bytes) {
		return 1;
	}

	if (bytes->size < ctx->minsz) {
		return 1;
	}

	it2 = r_sign_item_dup (it);
	r_list_append (ss->items, it2);

	// TODO(nibble): change arg data in r_search_keyword_new to void*
	kw = r_search_keyword_new (bytes->bytes, bytes->size, bytes->mask, bytes->size, (const char *) it2);
	r_search_kw_add (ss->search, kw);

	return 1;
}

R_API void r_sign_search_init(RAnal *a, RSignSearch *ss, int minsz, RSignSearchCallback cb, void *user) {
	struct ctxAddSearchKwCB ctx = { ss, minsz };

	if (!a || !ss || !cb) {
		return;
	}

	ss->cb = cb;
	ss->user = user;

	r_list_purge (ss->items);
	r_search_reset (ss->search, R_SEARCH_KEYWORD);

	r_sign_foreach (a, addSearchKwCB, &ctx);
	r_search_begin (ss->search);
	r_search_set_callback (ss->search, searchHitCB, ss);
}

R_API int r_sign_search_update(RAnal *a, RSignSearch *ss, ut64 *at, const ut8 *buf, int len) {
	if (!a || !ss || !buf || len <= 0) {
		return 0;
	}
	return r_search_update (ss->search, *at, buf, len);
}

// allow ~10% of margin error
static int matchCount(int a, int b) {
	int c = a - b;
	int m = a / 10;
	return R_ABS (c) < m;
}

static bool fcnMetricsCmp(RSignItem *it, RAnalFunction *fcn) {
	RSignGraph *graph = it->graph;
	int ebbs = -1;

	if (graph->cc != -1 && graph->cc != r_anal_fcn_cc (fcn)) {
		return false;
	}
	if (graph->nbbs != -1 && graph->nbbs != r_list_length (fcn->bbs)) {
		return false;
	}
	if (graph->edges != -1 && graph->edges != r_anal_fcn_count_edges (fcn, &ebbs)) {
		return false;
	}
	if (graph->ebbs != -1 && graph->ebbs != ebbs) {
		return false;
	}
	if (graph->bbsum > 0 && matchCount (graph->bbsum, r_anal_fcn_size (fcn))) {
		return false;
	}
	return true;
}

struct ctxFcnMatchCB {
	RAnal *anal;
	RAnalFunction *fcn;
	RSignGraphMatchCallback cb;
	void *user;
	int mincc;
};

static int graphMatchCB(RSignItem *it, void *user) {
	struct ctxFcnMatchCB *ctx = (struct ctxFcnMatchCB *) user;
	RSignGraph *graph = it->graph;

	if (!graph) {
		return 1;
	}

	if (graph->cc < ctx->mincc) {
		return 1;
	}

	if (!fcnMetricsCmp (it, ctx->fcn)) {
		return 1;
	}

	if (ctx->cb) {
		return ctx->cb (it, ctx->fcn, ctx->user);
	}

	return 1;
}

R_API bool r_sign_match_graph(RAnal *a, RAnalFunction *fcn, int mincc, RSignGraphMatchCallback cb, void *user) {
	struct ctxFcnMatchCB ctx = { a, fcn, cb, user, mincc };

	if (!a || !fcn || !cb) {
		return false;
	}

	return r_sign_foreach (a, graphMatchCB, &ctx);
}

static int addrMatchCB(RSignItem *it, void *user) {
	struct ctxFcnMatchCB *ctx = (struct ctxFcnMatchCB *) user;

	if (it->addr == UT64_MAX) {
		return 1;
	}

	if (it->addr != ctx->fcn->addr) {
		return 1;
	}

	if (ctx->cb) {
		return ctx->cb (it, ctx->fcn, ctx->user);
	}

	return 1;
}

R_API bool r_sign_match_addr(RAnal *a, RAnalFunction *fcn, RSignOffsetMatchCallback cb, void *user) {
	struct ctxFcnMatchCB ctx = { a, fcn, cb, user, 0 };

	if (!a || !fcn || !cb) {
		return false;
	}

	return r_sign_foreach (a, addrMatchCB, &ctx);
}

static int hashMatchCB(RSignItem *it, void *user) {
	struct ctxFcnMatchCB *ctx = (struct ctxFcnMatchCB *) user;
	RSignHash *hash = it->hash;

	if (!hash) {
		return 1;
	}

	if (!hash->bbhash || hash->bbhash[0] == 0) {
		return 1;
	}

	char *digest_hex = NULL;
	bool retval = false;
	digest_hex = r_sign_calc_bbhash (ctx->anal, ctx->fcn);
	if (strcmp (hash->bbhash, digest_hex)) {
		goto beach;
	}

	if (ctx->cb) {
		retval = ctx->cb (it, ctx->fcn, ctx->user);
	}
beach:
	free (digest_hex);
	return retval;
}

R_API bool r_sign_match_hash(RAnal *a, RAnalFunction *fcn, RSignHashMatchCallback cb, void *user) {
	struct ctxFcnMatchCB ctx = { a, fcn, cb, user, 0 };

	r_return_val_if_fail (a && fcn && cb, false);

	return r_sign_foreach (a, hashMatchCB, &ctx);
}


static int refsMatchCB(RSignItem *it, void *user) {
	struct ctxFcnMatchCB *ctx = (struct ctxFcnMatchCB *) user;
	RList *refs = NULL;
	char *ref_a = NULL, *ref_b = NULL;
	int i = 0, retval = 1;

	if (!it->refs) {
		return 1;
	}

	// TODO(nibble): slow operation, add cache
	refs = r_sign_fcn_refs (ctx->anal, ctx->fcn);
	if (!refs) {
		return 1;
	}

	for (i = 0; ; i++) {
		ref_a = (char *) r_list_get_n (it->refs, i);
		ref_b = (char *) r_list_get_n (refs, i);

		if (!ref_a || !ref_b) {
			if (ref_a != ref_b) {
				retval = 1;
				goto out;
			}
			break;
		}
		if (strcmp (ref_a, ref_b)) {
			retval = 1;
			goto out;
		}
	}

	if (ctx->cb) {
		retval = ctx->cb (it, ctx->fcn, ctx->user);
		goto out;
	}

out:
	r_list_free (refs);

	return retval;
}

R_API bool r_sign_match_refs(RAnal *a, RAnalFunction *fcn, RSignRefsMatchCallback cb, void *user) {
	r_return_val_if_fail (a && fcn && cb, false);
	struct ctxFcnMatchCB ctx = { a, fcn, cb, user, 0 };
	return r_sign_foreach (a, refsMatchCB, &ctx);
}

static int varsMatchCB(RSignItem *it, void *user) {
	struct ctxFcnMatchCB *ctx = (struct ctxFcnMatchCB *) user;
	RList *vars = NULL;
	char *var_a = NULL, *var_b = NULL;
	int i = 0, retval = 1;

	if (!it->vars) {
		return 1;
	}

	// TODO(nibble): slow operation, add cache
	vars = r_sign_fcn_vars (ctx->anal, ctx->fcn);
	if (!vars) {
		return 1;
	}

	for (i = 0; ; i++) {
		var_a = (char *) r_list_get_n (it->vars, i);
		var_b = (char *) r_list_get_n (vars, i);

		if (!var_a || !var_b) {
			if (var_a != var_b) {
				retval = 1;
				goto out;
			}
			break;
		}
		if (strcmp (var_a, var_b)) {
			retval = 1;
			goto out;
		}
	}

	if (ctx->cb) {
		retval = ctx->cb (it, ctx->fcn, ctx->user);
		goto out;
	}

out:
	r_list_free (vars);

	return retval;
}

R_API bool r_sign_match_vars(RAnal *a, RAnalFunction *fcn, RSignVarsMatchCallback cb, void *user) {
	r_return_val_if_fail (a && fcn && cb, false);
	struct ctxFcnMatchCB ctx = { a, fcn, cb, user, 0 };
	return r_sign_foreach (a, varsMatchCB, &ctx);
}


R_API RSignItem *r_sign_item_new() {
	RSignItem *ret = R_NEW0 (RSignItem);
	if (ret) {
		ret->addr = UT64_MAX;
		ret->space = NULL;
	}
	return ret;
}

R_API RSignItem *r_sign_item_dup(RSignItem *it) {
	RListIter *iter = NULL;
	char *ref = NULL;
	if (!it) {
		return NULL;
	}
	RSignItem *ret = r_sign_item_new ();
	if (!ret) {
		return NULL;
	}
	ret->name = r_str_new (it->name);
	ret->space = it->space;

	if (it->bytes) {
		ret->bytes = R_NEW0 (RSignBytes);
		if (!ret->bytes) {
			return NULL;
		}
		ret->bytes->size = it->bytes->size;
		ret->bytes->bytes = malloc (it->bytes->size);
		if (!ret->bytes->bytes) {
			r_sign_item_free (ret);
			return NULL;
		}
		memcpy (ret->bytes->bytes, it->bytes->bytes, it->bytes->size);
		ret->bytes->mask = malloc (it->bytes->size);
		if (!ret->bytes->mask) {
			r_sign_item_free (ret);
			return NULL;
		}
		memcpy (ret->bytes->mask, it->bytes->mask, it->bytes->size);
	}

	if (it->graph) {
		ret->graph = R_NEW0 (RSignGraph);
		if (!ret->graph) {
			r_sign_item_free (ret);
			return NULL;
		}
		*ret->graph = *it->graph;
	}

	ret->refs = r_list_newf ((RListFree) free);
	r_list_foreach (it->refs, iter, ref) {
		r_list_append (ret->refs, r_str_new (ref));
	}

	return ret;
}

R_API void r_sign_item_free(RSignItem *item) {
	if (!item) {
		return;
	}
	free (item->name);
	if (item->bytes) {
		free (item->bytes->bytes);
		free (item->bytes->mask);
		free (item->bytes);
	}
	if (item->hash) {
		free (item->hash->bbhash);
		free (item->hash);
	}
	free (item->graph);
	r_list_free (item->refs);
	r_list_free (item->vars);
	free (item);
}

static int loadCB(void *user, const char *k, const char *v) {
	RAnal *a = (RAnal *) user;
	char nk[R_SIGN_KEY_MAXSZ], nv[R_SIGN_VAL_MAXSZ];
	RSignItem *it = r_sign_item_new ();
	if (it && deserialize (a, it, k, v)) {
		serialize (a, it, nk, nv);
		sdb_set (a->sdb_zigns, nk, nv, 0);
	} else {
		eprintf ("error: cannot deserialize zign\n");
	}
	r_sign_item_free (it);
	return 1;
}

R_API char *r_sign_path(RAnal *a, const char *file) {
	char *abs = r_file_abspath (file);
	if (abs) {
		if (r_file_is_regular (abs)) {
			return abs;
		}
		free (abs);
	}

	if (a->zign_path) {
		char *path = r_str_newf ("%s%s%s", a->zign_path, R_SYS_DIR, file);
		abs = r_file_abspath (path);
		free (path);
		if (r_file_is_regular (abs)) {
			return abs;
		}
		free (abs);
	} else {
		char *home = r_str_home (R2_HOME_ZIGNS);
		abs = r_str_newf ("%s%s%s", home, R_SYS_DIR, file);
		free (home);
		if (r_file_is_regular (abs)) {
			return abs;
		}
		free (abs);
	}

	abs = r_str_newf (R_JOIN_3_PATHS ("%s", R2_ZIGNS, "%s"), r_sys_prefix (NULL), file);
	if (r_file_is_regular (abs)) {
		return abs;
	}
	free (abs);

	return NULL;
}

R_API bool r_sign_load(RAnal *a, const char *file) {
	if (!a || !file) {
		return false;
	}
	char *path = r_sign_path (a, file);
	if (!r_file_exists (path)) {
		eprintf ("error: file %s does not exist\n", file);
		free (path);
		return false;
	}
	Sdb *db = sdb_new (NULL, path, 0);
	if (!db) {
		free (path);
		return false;
	}
	sdb_foreach (db, loadCB, a);
	sdb_close (db);
	sdb_free (db);
	free (path);
	return true;
}

R_API bool r_sign_load_gz(RAnal *a, const char *filename) {
	ut8 *buf = NULL;
	int size = 0;
	char *tmpfile = NULL;
	bool retval = true;

	char *path = r_sign_path (a, filename);
	if (!r_file_exists (path)) {
		eprintf ("error: file %s does not exist\n", filename);
		retval = false;
		goto out;
	}

	if (!(buf = r_file_gzslurp (path, &size, 0))) {
		eprintf ("error: cannot decompress file\n");
		retval = false;
		goto out;
	}

	if (!(tmpfile = r_file_temp ("r2zign"))) {
		eprintf ("error: cannot create temp file\n");
		retval = false;
		goto out;
	}

	if (!r_file_dump (tmpfile, buf, size, 0)) {
		eprintf ("error: cannot dump file\n");
		retval = false;
		goto out;
	}

	if (!r_sign_load (a, tmpfile)) {
		eprintf ("error: cannot load file\n");
		retval = false;
		goto out;
	}

	if (!r_file_rm (tmpfile)) {
		eprintf ("error: cannot delete temp file\n");
		retval = false;
		goto out;
	}

out:
	free (buf);
	free (tmpfile);
	free (path);

	return retval;
}

R_API bool r_sign_save(RAnal *a, const char *file) {
	r_return_val_if_fail (a && file, false);

	if (sdb_isempty (a->sdb_zigns)) {
		eprintf ("WARNING: no zignatures to save\n");
		return false;
	}

	Sdb *db = sdb_new (NULL, file, 0);
	if (!db) {
		return false;
	}
	sdb_merge (db, a->sdb_zigns);
	bool retval = sdb_sync (db);
	sdb_close (db);
	sdb_free (db);

	return retval;
}
