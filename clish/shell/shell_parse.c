/*
 * shell_parse.c
 */

#include <string.h>
#include <assert.h>

#include "lub/string.h"
#include "lub/system.h"
#include "private.h"

/*----------------------------------------------------------- */
clish_pargv_status_t clish_shell_parse(
	clish_shell_t *this, const char *line,
	const clish_command_t **ret_cmd, clish_pargv_t **pargv)
{
	clish_pargv_status_t result = CLISH_BAD_CMD;
	clish_context_t context;
	const clish_command_t *cmd;
	lub_argv_t *argv = NULL;
	unsigned int idx;

	*ret_cmd = cmd = clish_shell_resolve_command(this, line);
	if (!cmd)
		return result;

	/* Now construct the parameters for the command */
	*pargv = clish_pargv_new();
	context.shell = this;
	context.cmd = cmd;
	context.pargv = *pargv;

	idx = lub_argv_wordcount(clish_command__get_name(cmd));
	argv = lub_argv_new(line, 0);
	result = clish_shell_parse_pargv(*pargv, cmd, &context,
		clish_command__get_paramv(cmd),
		argv, &idx, NULL, 0);
	lub_argv_delete(argv);
	if (CLISH_LINE_OK != result) {
		clish_pargv_delete(*pargv);
		*pargv = NULL;
	}
	if (*pargv) {
		char str[100];
		char * tmp;
		/* Variable __cur_depth */
		int depth = clish_shell__get_depth(this);
		snprintf(str, sizeof(str) - 1, "%u", depth);
		clish_pargv_insert(*pargv, this->param_depth, str);
		/* Variable __cur_pwd */
		tmp = clish_shell__get_pwd_full(this, depth);
		if (tmp) {
			clish_pargv_insert(*pargv, this->param_pwd, tmp);
			lub_string_free(tmp);
		}
		/* Variable __interactive */
		if (clish_shell__get_interactive(this))
			tmp = "1";
		else
			tmp = "0";
		clish_pargv_insert(*pargv, this->param_interactive, tmp);
	}

	return result;
}

/*--------------------------------------------------------- */
clish_pargv_status_t clish_shell_parse_pargv(clish_pargv_t *pargv,
	const clish_command_t *cmd,
	void *context,
	clish_paramv_t *paramv,
	const lub_argv_t *argv,
	unsigned *idx, clish_pargv_t *last, unsigned need_index)
{
	unsigned argc = lub_argv__get_count(argv);
	unsigned index = 0;
	unsigned nopt_index = 0;
	clish_param_t *nopt_param = NULL;
	unsigned i;
	clish_pargv_status_t retval;
	unsigned paramc = clish_paramv__get_count(paramv);
	int up_level = 0; /* Is it a first level of param nesting? */

	assert(pargv);
	assert(cmd);

	/* Check is it a first level of PARAM nesting. */
	if (paramv == clish_command__get_paramv(cmd))
		up_level = 1;

	while (index < paramc) {
		const char *arg = NULL;
		clish_param_t *param = clish_paramv__get_param(paramv,index);
		clish_param_t *cparam = NULL;
		int is_switch = 0;

		/* Use real arg or PARAM's default value as argument */
		if (*idx < argc)
			arg = lub_argv__get_arg(argv, *idx);

		/* Is parameter in "switch" mode? */
		if (CLISH_PARAM_SWITCH == clish_param__get_mode(param))
			is_switch = 1;

		/* Check the 'test' conditions */
		if (param) {
			char *str = clish_shell_expand(clish_param__get_test(param), SHELL_VAR_ACTION, context);
			if (str && !lub_system_line_test(str)) {
				lub_string_free(str);
				index++;
				continue;
			}
			lub_string_free(str);
		}

		/* Add param for help and completion */
		if (last && (*idx == need_index) &&
			(NULL == clish_pargv_find_arg(pargv, clish_param__get_name(param)))) {
			if (is_switch) {
				unsigned rec_paramc = clish_param__get_param_count(param);
				for (i = 0; i < rec_paramc; i++) {
					cparam = clish_param__get_param(param, i);
					if (!cparam)
						break;
					if (CLISH_PARAM_SUBCOMMAND ==
						clish_param__get_mode(cparam)) {
						const char *pname =
							clish_param__get_value(cparam);
						if (!arg || (arg && 
							(pname == lub_string_nocasestr(pname,
							arg))))
							clish_pargv_insert(last,
								cparam, arg);
					} else {
						clish_pargv_insert(last,
							cparam, arg);
					}
				}
			} else {
				if (CLISH_PARAM_SUBCOMMAND ==
					clish_param__get_mode(param)) {
					const char *pname =
					    clish_param__get_value(param);
					if (!arg || (arg &&
						(pname == lub_string_nocasestr(pname, arg))))
						clish_pargv_insert(last, param, arg);
				} else {
					clish_pargv_insert(last, param, arg);
				}
			}
		}

		/* Set parameter value */
		if (param) {
			char *validated = NULL;
			clish_paramv_t *rec_paramv =
			    clish_param__get_paramv(param);
			unsigned rec_paramc =
			    clish_param__get_param_count(param);

			/* Save the index of last non-option parameter
			 * to restore index if the optional parameters
			 * will be used.
			 */
			if (BOOL_TRUE != clish_param__get_optional(param)) {
				nopt_param = param;
				nopt_index = index;
			}

			/* Validate the current parameter. */
			if (NULL != clish_pargv_find_arg(pargv, clish_param__get_name(param))) {
				/* Duplicated parameter */
				validated = NULL;
			} else if (is_switch) {
				for (i = 0; i < rec_paramc; i++) {
					cparam =
					    clish_param__get_param(param, i);
					if (!cparam)
						break;
					if ((validated =
					    arg ? clish_param_validate(cparam,
								       arg) :
					    NULL)) {
						rec_paramv =
						    clish_param__get_paramv
						    (cparam);
						rec_paramc =
						    clish_param__get_param_count
						    (cparam);
						break;
					}
				}
			} else {
				validated =
				    arg ? clish_param_validate(param,
							       arg) : NULL;
			}

			if (validated) {
				/* add (or update) this parameter */
				if (is_switch) {
					clish_pargv_insert(pargv, param,
						clish_param__get_name(cparam));
					clish_pargv_insert(pargv, cparam,
						validated);
				} else {
					clish_pargv_insert(pargv, param,
						validated);
				}
				lub_string_free(validated);

				/* Next command line argument */
				/* Don't change idx if this is the last
				   unfinished optional argument.
				 */
				if (!(clish_param__get_optional(param) &&
					(*idx == need_index) &&
					(need_index == (argc - 1))))
				(*idx)++;

				/* Walk through the nested parameters */
				if (rec_paramc) {
					retval = clish_shell_parse_pargv(pargv, cmd,
						context, rec_paramv,
						argv, idx, last, need_index);
					if (CLISH_LINE_OK != retval)
						return retval;
				}

				/* Choose the next parameter */
				if (BOOL_TRUE == clish_param__get_optional(param)) {
					if (nopt_param)
						index = nopt_index + 1;
					else
						index = 0;
				} else {
					index++;
				}

			} else {
				/* Choose the next parameter if current
				 * is not validated.
				 */
				if (BOOL_TRUE ==
					clish_param__get_optional(param))
					index++;
				else {
					if (!arg)
						break;
					else
						return CLISH_BAD_PARAM;
				}
			}
		} else {
			return CLISH_BAD_PARAM;
		}
	}

	/* Check for non-optional parameters without values */
	if ((*idx >= argc) && (index < paramc)) {
		unsigned j = index;
		const clish_param_t *param;
		while (j < paramc) {
			param = clish_paramv__get_param(paramv, j++);
			if (BOOL_TRUE != clish_param__get_optional(param))
				return CLISH_LINE_PARTIAL;
		}
	}

	/* If the number of arguments is bigger than number of
	 * params than it's a args. So generate the args entry
	 * in the list of completions.
	 */
	if (last && up_level &&
			clish_command__get_args(cmd) &&
			(clish_pargv__get_count(last) == 0) &&
			(*idx <= argc) && (index >= paramc)) {
		clish_pargv_insert(last, clish_command__get_args(cmd), "");
	}

	/*
	 * if we've satisfied all the parameters we can now construct
	 * an 'args' parameter if one exists
	 */
	if (up_level && (*idx < argc) && (index >= paramc)) {
		const char *arg = lub_argv__get_arg(argv, *idx);
		const clish_param_t *param = clish_command__get_args(cmd);
		char *args = NULL;

		if (!param)
			return CLISH_BAD_CMD;

		/*
		 * put all the argument into a single string
		 */
		while (NULL != arg) {
			bool_t quoted = lub_argv__get_quoted(argv, *idx);
			if (BOOL_TRUE == quoted) {
				lub_string_cat(&args, "\"");
			}
			/* place the current argument in the string */
			lub_string_cat(&args, arg);
			if (BOOL_TRUE == quoted) {
				lub_string_cat(&args, "\"");
			}
			(*idx)++;
			arg = lub_argv__get_arg(argv, *idx);
			if (NULL != arg) {
				/* add a space if there are more arguments */
				lub_string_cat(&args, " ");
			}
		}
		/* add (or update) this parameter */
		clish_pargv_insert(pargv, param, args);
		lub_string_free(args);
	}

	return CLISH_LINE_OK;
}

/*----------------------------------------------------------- */
clish_shell_state_t clish_shell__get_state(const clish_shell_t *this)
{
	return this->state;
}

/*----------------------------------------------------------- */
void clish_shell__set_state(clish_shell_t *this,
	clish_shell_state_t state)
{
	assert(this);
	this->state = state;
}

/*----------------------------------------------------------- */
