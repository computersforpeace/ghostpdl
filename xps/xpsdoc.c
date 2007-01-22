#include "ghostxps.h"

#include <expat.h>

/*
 * Content types are stored in two separate binary trees.
 * One lists Override entries, which map a part name to a type.
 * The other lists Default entries, which map a file extension to a type.
 */

void xps_debug_type_map(xps_context_t *ctx, char *label, xps_type_map_t *node)
{
    if (node)
    {
	if (node->left)
	    xps_debug_type_map(ctx, label, node->left);
	dprintf3("%s name=%s type=%s\n", label, node->name, node->type);
	if (node->right)
	    xps_debug_type_map(ctx, label, node->right);
    }
}

static xps_type_map_t *
xps_new_type_map(xps_context_t *ctx, char *name, char *type)
{
    xps_type_map_t *node;

    node = xps_alloc(ctx, sizeof(xps_type_map_t));
    if (!node)
	goto cleanup;

    node->name = xps_strdup(ctx, name);
    node->type = xps_strdup(ctx, type);
    node->left = NULL;
    node->right = NULL;

    if (!node->name)
	goto cleanup;
    if (!node->type)
	goto cleanup;

    return node;

cleanup:
    if (node)
    {
	if (node->name) xps_free(ctx, node->name);
	if (node->type) xps_free(ctx, node->type);
	xps_free(ctx, node);
    }
    return NULL;
}

static void
xps_add_type_map(xps_context_t *ctx, xps_type_map_t *node, char *name, char *type)
{
    int cmp = strcmp(node->name, name);
    if (cmp < 0)
    {
	if (node->left)
	    xps_add_type_map(ctx, node->left, name, type);
	else
	    node->left = xps_new_type_map(ctx, name, type);
    }
    else if (cmp > 0)
    {
	if (node->right)
	    xps_add_type_map(ctx, node->right, name, type);
	else
	    node->right = xps_new_type_map(ctx, name, type);
    }
    else
    {
	/* it's a duplicate so we don't do anything */
    }
}

static void
xps_add_override(xps_context_t *ctx, char *part_name, char *content_type)
{
    /* dprintf2("Override part=%s type=%s\n", part_name, content_type); */
    if (ctx->overrides == NULL)
	ctx->overrides = xps_new_type_map(ctx, part_name, content_type);
    else
	xps_add_type_map(ctx, ctx->overrides, part_name, content_type);
}

static void
xps_add_default(xps_context_t *ctx, char *extension, char *content_type)
{
    /* dprintf2("Default extension=%s type=%s\n", extension, content_type); */
    if (ctx->defaults == NULL)
	ctx->defaults = xps_new_type_map(ctx, extension, content_type);
    else
	xps_add_type_map(ctx, ctx->defaults, extension, content_type);
}

/*
 * Relationships are stored in a binary tree indexed by the source and target pair.
 */

void xps_debug_relations(xps_context_t *ctx, xps_relation_t *node)
{
    if (node)
    {
	if (node->left)
	    xps_debug_relations(ctx, node->left);
	dprintf3("Relation source=%s target=%s type=%s\n", node->source, node->target, node->type);
	if (node->right)
	    xps_debug_relations(ctx, node->right);
    }
}

static inline int
xps_compare_relation(xps_relation_t *a, char *b_source, char *b_target)
{
    int cmp = strcmp(a->source, b_source);
    if (cmp == 0)
	cmp = strcmp(a->target, b_target);
    return cmp;
}

static xps_relation_t *
xps_new_relation(xps_context_t *ctx, char *source, char *target, char *type)
{
    xps_relation_t *node;

    node = xps_alloc(ctx, sizeof(xps_relation_t));
    if (!node)
	goto cleanup;

    node->source = xps_strdup(ctx, source);
    node->target = xps_strdup(ctx, target);
    node->type = xps_strdup(ctx, type);
    node->left = NULL;
    node->right = NULL;

    if (!node->source)
	goto cleanup;
    if (!node->target)
	goto cleanup;
    if (!node->type)
	goto cleanup;

    return node;

cleanup:
    if (node)
    {
	if (node->source) xps_free(ctx, node->source);
	if (node->target) xps_free(ctx, node->target);
	if (node->type) xps_free(ctx, node->type);
	xps_free(ctx, node);
    }
    return NULL;
}


static void
xps_add_relation_imp(xps_context_t *ctx, xps_relation_t *node, char *source, char *target, char *type)
{
    int cmp = xps_compare_relation(node, source, target);
    if (cmp < 0)
    {
	if (node->left)
	    xps_add_relation_imp(ctx, node->left, source, target, type);
	else
	    node->left = xps_new_relation(ctx, source, target, type);
    }
    else if (cmp > 0)
    {
	if (node->right)
	    xps_add_relation_imp(ctx, node->right, source, target, type);
	else
	    node->right = xps_new_relation(ctx, source, target, type);
    }
    else
    {
	/* it's a duplicate so we don't do anything */
    }
}

static void
xps_add_relation(xps_context_t *ctx, char *source, char *target, char *type)
{
    /* dprintf3("Relation source=%s target=%s type=%s\n", source, target, type); */
    if (ctx->relations == NULL)
	ctx->relations = xps_new_relation(ctx, source, target, type);
    else
	xps_add_relation_imp(ctx, ctx->relations, source, target, type);
}

/*
 * Parse the metadata [Content_Types.xml] and _rels/XXX.rels parts.
 * These should be parsed eagerly as they are interleaved, so the
 * parsing needs to be able to cope with incomplete xml.
 *
 * We re-parse the part every time a new part of the piece comes in.
 * The binary trees in xps_context_t make sure that the information
 * is not duplicated (since the entries are often parsed many times).
 *
 * We hook up unique expat handlers for this, and ignore any expat
 * errors that occur.
 */

static void
xps_handle_metadata(void *zp, char *name, char **atts)
{
    xps_context_t *ctx = zp;
    int i;

    if (!strcmp(name, "Default"))
    {
	char *extension = NULL;
	char *type = NULL;

	for (i = 0; atts[i]; i += 2)
	{
	    if (!strcmp(atts[i], "Extension"))
		extension = atts[i + 1];
	    if (!strcmp(atts[i], "ContentType"))
		type = atts[i + 1];
	}

	if (extension && type)
	    xps_add_default(ctx, extension, type);
    }

    if (!strcmp(name, "Override"))
    {
	char *partname = NULL;
	char *type = NULL;

	for (i = 0; atts[i]; i += 2)
	{
	    if (!strcmp(atts[i], "PartName"))
		partname = atts[i + 1];
	    if (!strcmp(atts[i], "ContentType"))
		type = atts[i + 1];
	}

	if (partname && type)
	    xps_add_override(ctx, partname, type);
    }

    if (!strcmp(name, "Relationship"))
    {
	char source[1024];
	char *target = NULL;
	char *type = NULL;

	strcpy(source, ctx->last->name);

	for (i = 0; atts[i]; i += 2)
	{
	    if (!strcmp(atts[i], "Target"))
		target = atts[i + 1];
	    if (!strcmp(atts[i], "Type"))
		type = atts[i + 1];
	}

	if (target && type)
	    xps_add_relation(ctx, source, target, type);
    }
}

static int
xps_process_metadata(xps_context_t *ctx, xps_part_t *part)
{
    XML_Parser xp;

    xp = XML_ParserCreate(NULL);
    if (!xp)
	return -1;

    XML_SetUserData(xp, ctx);
    XML_SetParamEntityParsing(xp, XML_PARAM_ENTITY_PARSING_NEVER);
    XML_SetStartElementHandler(xp, xps_handle_metadata);

    (void) XML_Parse(xp, part->data, part->size, 1);

    XML_ParserFree(xp);

    return 0;
}

/*
 * The document sequence bla bla bla.
 */

int
xps_process_fpage(xps_context_t *ctx, xps_part_t *part)
{
    xps_parser_t *parser;
    parser = xps_new_parser(ctx, part->data, part->size);
    if (!parser)
	return gs_rethrow(-1, "could not create xml parser");

//    xps_debug_parser(parser);

    return 0;
}

int
xps_process_part(xps_context_t *ctx, xps_part_t *part)
{
    if (strstr(part->name, "[Content_Types].xml"))
    {
	xps_process_metadata(ctx, part);
    }

    if (strstr(part->name, "_rels/"))
    {
	xps_process_metadata(ctx, part);
    }

    if (part->complete)
    {
	dprintf1("complete part '%s'.\n", part->name);
    }
    else
    {
	dprintf1("incomplete piece '%s'.\n", part->name);
    }

}

