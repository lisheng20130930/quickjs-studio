#include "editor.h"

#include "SciLexer.h"
#include "Scintilla.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARKER_BREAKPOINT 0
#define MARKER_EXECUTION 1
#define MARKER_BOOKMARK 2
#define INDICATOR_CLASS 8
#define INDICATOR_VARIABLE 9
#define INDICATOR_METHOD 10
#define INDICATOR_MACRO 11

typedef void *(__stdcall *CreateLexerFn)(const char *name);

static HMODULE scintilla_module;
static HMODULE lexilla_module;
static CreateLexerFn create_lexer;
static char current_path[MAX_PATH];

static LRESULT sci(HWND editor, UINT message, WPARAM wparam, LPARAM lparam)
{
    return SendMessageA(editor, message, wparam, lparam);
}

static COLORREF darker_window_color(COLORREF color)
{
    return RGB(GetRValue(color) * 94 / 100, GetGValue(color) * 94 / 100,
               GetBValue(color) * 94 / 100);
}

static int identifier_character(unsigned char character)
{
    return isalnum(character) || character == '_' || character == '$' || character >= 0x80;
}

static int previous_word_is(const char *text, size_t position, const char *word)
{
    size_t end = position;
    size_t start;
    size_t length = strlen(word);

    while (end > 0 && isspace((unsigned char)text[end - 1]))
        end--;
    start = end;
    while (start > 0 && identifier_character((unsigned char)text[start - 1]))
        start--;
    return end - start == length && strncmp(text + start, word, length) == 0;
}

static void fill_text_indicator(HWND editor, int indicator, LRESULT start, LRESULT length)
{
    sci(editor, SCI_SETINDICATORCURRENT, indicator, 0);
    sci(editor, SCI_INDICATORFILLRANGE, (WPARAM)start, length);
}

void editor_apply_semantic_colors(HWND editor)
{
    LRESULT length = sci(editor, SCI_GETLENGTH, 0, 0);
    char *text;

    if (length <= 0)
        return;
    text = malloc((size_t)length + 1);
    if (!text)
        return;
    sci(editor, SCI_GETTEXT, (WPARAM)length + 1, (LPARAM)text);
    sci(editor, SCI_COLOURISE, 0, -1);
    for (int indicator = INDICATOR_CLASS; indicator <= INDICATOR_MACRO; indicator++) {
        sci(editor, SCI_SETINDICATORCURRENT, indicator, 0);
        sci(editor, SCI_INDICATORCLEARRANGE, 0, length);
    }
    for (LRESULT position = 0; position < length;) {
        LRESULT end;
        LRESULT next;
        int indicator;
        int has_lower = 0;
        int has_upper = 0;
        int has_underscore = 0;

        if ((int)sci(editor, SCI_GETSTYLEAT, position, 0) != SCE_C_IDENTIFIER ||
            !identifier_character((unsigned char)text[position])) {
            position++;
            continue;
        }
        end = position + 1;
        while (end < length && identifier_character((unsigned char)text[end]))
            end++;
        for (LRESULT index = position; index < end; index++) {
            has_lower |= islower((unsigned char)text[index]) != 0;
            has_upper |= isupper((unsigned char)text[index]) != 0;
            has_underscore |= text[index] == '_';
        }
        next = end;
        while (next < length && isspace((unsigned char)text[next]))
            next++;
        if (previous_word_is(text, (size_t)position, "class") ||
            previous_word_is(text, (size_t)position, "new") ||
            previous_word_is(text, (size_t)position, "extends") ||
            previous_word_is(text, (size_t)position, "instanceof")) {
            indicator = INDICATOR_CLASS;
        } else if (next < length && text[next] == '(') {
            indicator = INDICATOR_METHOD;
        } else if (!has_lower && has_upper && (has_underscore || end - position > 1)) {
            indicator = INDICATOR_MACRO;
        } else if (isupper((unsigned char)text[position])) {
            indicator = INDICATOR_CLASS;
        } else {
            indicator = INDICATOR_VARIABLE;
        }
        fill_text_indicator(editor, indicator, position, end - position);
        position = end;
    }
    free(text);
}

void editor_update_brace_highlight(HWND editor)
{
    LRESULT caret = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    LRESULT brace = -1;
    LRESULT match;
    int character;

    if (caret > 0) {
        character = (int)sci(editor, SCI_GETCHARAT, caret - 1, 0);
        if (strchr("()[]{}", character))
            brace = caret - 1;
    }
    if (brace < 0) {
        character = (int)sci(editor, SCI_GETCHARAT, caret, 0);
        if (strchr("()[]{}", character))
            brace = caret;
    }
    if (brace < 0) {
        sci(editor, SCI_BRACEHIGHLIGHT, -1, -1);
        return;
    }
    match = sci(editor, SCI_BRACEMATCH, brace, 0);
    if (match >= 0)
        sci(editor, SCI_BRACEHIGHLIGHT, brace, match);
    else
        sci(editor, SCI_BRACEBADLIGHT, brace, 0);
}

static int complete_brace_block(HWND editor)
{
    LRESULT position = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    int line = (int)sci(editor, SCI_LINEFROMPOSITION, position, 0);
    LRESULT previous_end;
    LRESULT line_start;
    LRESULT line_end;
    LRESULT first_content;
    LRESULT inner_position;
    LRESULT matching_brace;
    int base_indent;
    int eol_mode;
    const char *eol;
    size_t eol_length;
    char *closing;

    if (line <= 0)
        return 0;
    previous_end = sci(editor, SCI_GETLINEENDPOSITION, line - 1, 0);
    while (previous_end > 0) {
        int character = (int)sci(editor, SCI_GETCHARAT, previous_end - 1, 0);
        if (character != ' ' && character != '\t')
            break;
        previous_end--;
    }
    if (previous_end <= 0 || sci(editor, SCI_GETCHARAT, previous_end - 1, 0) != '{')
        return 0;

    line_start = sci(editor, SCI_POSITIONFROMLINE, line, 0);
    line_end = sci(editor, SCI_GETLINEENDPOSITION, line, 0);
    first_content = line_start;
    while (first_content < line_end) {
        int character = (int)sci(editor, SCI_GETCHARAT, first_content, 0);
        if (character != ' ' && character != '\t')
            break;
        first_content++;
    }
    base_indent = (int)sci(editor, SCI_GETLINEINDENTATION, line - 1, 0);
    eol_mode = (int)sci(editor, SCI_GETEOLMODE, 0, 0);
    eol = eol_mode == SC_EOL_CRLF ? "\r\n" : (eol_mode == SC_EOL_CR ? "\r" : "\n");
    eol_length = strlen(eol);
    matching_brace = sci(editor, SCI_BRACEMATCH, previous_end - 1, 0);

    if (matching_brace >= 0) {
        if (first_content < line_end && (first_content != matching_brace ||
                                         sci(editor, SCI_GETCHARAT, first_content, 0) != '}'))
            return 0;
        sci(editor, SCI_BEGINUNDOACTION, 0, 0);
        if (first_content == matching_brace) {
            sci(editor, SCI_INSERTTEXT, line_start, (LPARAM)eol);
            sci(editor, SCI_SETLINEINDENTATION, line + 1, base_indent);
        }
        sci(editor, SCI_SETLINEINDENTATION, line, base_indent + 4);
        inner_position = sci(editor, SCI_GETLINEINDENTPOSITION, line, 0);
        sci(editor, SCI_SETSEL, inner_position, inner_position);
        sci(editor, SCI_ENDUNDOACTION, 0, 0);
        return 1;
    }

    if (first_content < line_end)
        return 0;
    closing = (char *)malloc(eol_length + (size_t)base_indent + 2);
    if (!closing)
        return 0;
    memcpy(closing, eol, eol_length);
    memset(closing + eol_length, ' ', (size_t)base_indent);
    closing[eol_length + (size_t)base_indent] = '}';
    closing[eol_length + (size_t)base_indent + 1] = '\0';

    sci(editor, SCI_BEGINUNDOACTION, 0, 0);
    sci(editor, SCI_SETLINEINDENTATION, line, base_indent + 4);
    inner_position = sci(editor, SCI_GETLINEINDENTPOSITION, line, 0);
    sci(editor, SCI_INSERTTEXT, inner_position, (LPARAM)closing);
    sci(editor, SCI_SETSEL, inner_position, inner_position);
    sci(editor, SCI_ENDUNDOACTION, 0, 0);
    free(closing);
    return 1;
}

void editor_handle_newline(HWND editor)
{
    LRESULT position;
    LRESULT previous_end;
    LRESULT indentation_position;
    int line;
    int indentation;

    if (complete_brace_block(editor))
        return;
    position = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    line = (int)sci(editor, SCI_LINEFROMPOSITION, position, 0);
    if (line <= 0)
        return;
    indentation = (int)sci(editor, SCI_GETLINEINDENTATION, line - 1, 0);
    previous_end = sci(editor, SCI_GETLINEENDPOSITION, line - 1, 0);
    while (previous_end > 0) {
        int character = (int)sci(editor, SCI_GETCHARAT, previous_end - 1, 0);
        if (character != ' ' && character != '\t')
            break;
        previous_end--;
    }
    if (previous_end > 0 && sci(editor, SCI_GETCHARAT, previous_end - 1, 0) == '{')
        indentation += 4;

    sci(editor, SCI_BEGINUNDOACTION, 0, 0);
    sci(editor, SCI_SETLINEINDENTATION, line, indentation);
    indentation_position = sci(editor, SCI_GETLINEINDENTPOSITION, line, 0);
    sci(editor, SCI_SETSEL, indentation_position, indentation_position);
    sci(editor, SCI_ENDUNDOACTION, 0, 0);
}

void editor_handle_closing_brace(HWND editor)
{
    LRESULT position = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    LRESULT brace = position - 1;
    LRESULT line_start;
    LRESULT match;
    LRESULT new_brace;
    int line;
    int indentation;

    if (brace < 0 || sci(editor, SCI_GETCHARAT, brace, 0) != '}')
        return;
    line = (int)sci(editor, SCI_LINEFROMPOSITION, brace, 0);
    line_start = sci(editor, SCI_POSITIONFROMLINE, line, 0);
    for (LRESULT cursor = line_start; cursor < brace; cursor++) {
        int character = (int)sci(editor, SCI_GETCHARAT, cursor, 0);
        if (character != ' ' && character != '\t')
            return;
    }
    match = sci(editor, SCI_BRACEMATCH, brace, 0);
    if (match < 0)
        return;
    indentation =
        (int)sci(editor, SCI_GETLINEINDENTATION, sci(editor, SCI_LINEFROMPOSITION, match, 0), 0);
    sci(editor, SCI_BEGINUNDOACTION, 0, 0);
    sci(editor, SCI_SETLINEINDENTATION, line, indentation);
    new_brace = sci(editor, SCI_GETLINEINDENTPOSITION, line, 0);
    sci(editor, SCI_SETSEL, new_brace + 1, new_brace + 1);
    sci(editor, SCI_ENDUNDOACTION, 0, 0);
}

void editor_get_view_state(HWND editor, EditorViewState *state)
{
    if (!state)
        return;
    state->position = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    state->anchor = sci(editor, SCI_GETANCHOR, 0, 0);
    state->first_visible_line = (int)sci(editor, SCI_GETFIRSTVISIBLELINE, 0, 0);
    state->x_offset = (int)sci(editor, SCI_GETXOFFSET, 0, 0);
}

void editor_set_view_state(HWND editor, const EditorViewState *state)
{
    LRESULT length;
    LRESULT position;
    LRESULT anchor;

    if (!state)
        return;
    length = sci(editor, SCI_GETLENGTH, 0, 0);
    position = state->position < 0 ? 0 : (state->position > length ? length : state->position);
    anchor = state->anchor < 0 ? 0 : (state->anchor > length ? length : state->anchor);
    sci(editor, SCI_SETSEL, anchor, position);
    sci(editor, SCI_SETFIRSTVISIBLELINE,
        state->first_visible_line < 0 ? 0 : state->first_visible_line, 0);
    sci(editor, SCI_SETXOFFSET, state->x_offset < 0 ? 0 : state->x_offset, 0);
    SetFocus(editor);
}

void editor_update_line_number_margin(HWND editor)
{
    int line_count = (int)sci(editor, SCI_GETLINECOUNT, 0, 0);
    int digits = 1;
    char sample[16];
    int width;

    while (line_count >= 10) {
        line_count /= 10;
        digits++;
    }
    if (digits < 3)
        digits = 3;
    if (digits > (int)sizeof(sample) - 1)
        digits = (int)sizeof(sample) - 1;
    memset(sample, '9', (size_t)digits);
    sample[digits] = '\0';
    width = (int)sci(editor, SCI_TEXTWIDTH, STYLE_LINENUMBER, (LPARAM)sample) + 6;
    sci(editor, SCI_SETMARGINWIDTHN, 0, width);
}

int editor_load_components(void)
{
    scintilla_module = LoadLibraryA("Scintilla.dll");
    if (!scintilla_module)
        return 0;
    lexilla_module = LoadLibraryA("lexilla.dll");
    if (!lexilla_module)
        goto fail;
    FARPROC procedure = GetProcAddress(lexilla_module, "CreateLexer");
    memcpy(&create_lexer, &procedure, sizeof(create_lexer));
    if (!create_lexer)
        goto fail;
    return 1;

fail:
    editor_unload_components();
    return 0;
}

void editor_unload_components(void)
{
    if (lexilla_module)
        FreeLibrary(lexilla_module);
    if (scintilla_module)
        FreeLibrary(scintilla_module);
    lexilla_module = NULL;
    scintilla_module = NULL;
}

static void configure_styles(HWND editor)
{
    static const char keywords[] =
        "as async await break case catch class const continue debugger "
        "default delete do else export extends false finally for from "
        "function get if import in instanceof let new null of return set "
        "static super switch this throw true try typeof undefined var void "
        "while with yield";
    void *lexer = create_lexer ? create_lexer("cpp") : NULL;
    COLORREF window_background = GetSysColor(COLOR_WINDOW);
    COLORREF margin_background = darker_window_color(window_background);
    COLORREF window_text = GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF assist_class = RGB(0, 0, 255);
    const COLORREF assist_variable = RGB(0, 0, 128);
    const COLORREF assist_method = RGB(128, 64, 0);
    const COLORREF assist_macro = RGB(220, 0, 180);

    sci(editor, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    sci(editor, SCI_SETFONTQUALITY, SC_EFF_QUALITY_NON_ANTIALIASED, 0);
    sci(editor, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM) "Fixedsys");
    sci(editor, SCI_STYLESETSIZE, STYLE_DEFAULT, 9);
    sci(editor, SCI_STYLESETBACK, STYLE_DEFAULT, window_background);
    sci(editor, SCI_STYLESETFORE, STYLE_DEFAULT, window_text);
    sci(editor, SCI_STYLECLEARALL, 0, 0);
    sci(editor, SCI_SETCARETFORE, window_text, 0);
    sci(editor, SCI_SETSELFORE, TRUE, RGB(255, 255, 255));
    sci(editor, SCI_SETSELBACK, TRUE, RGB(0, 0, 128));
    sci(editor, SCI_SETILEXER, 0, (LPARAM)lexer);
    sci(editor, SCI_SETPROPERTY, (WPARAM) "lexer.cpp.allow.dollars", (LPARAM) "1");
    sci(editor, SCI_SETPROPERTY, (WPARAM) "lexer.cpp.enable.preprocessor", (LPARAM) "0");
    sci(editor, SCI_SETPROPERTY, (WPARAM) "lexer.cpp.backquoted.strings", (LPARAM) "2");
    sci(editor, SCI_SETPROPERTY, (WPARAM) "lexer.cpp.escape.sequence", (LPARAM) "1");
    sci(editor, SCI_SETKEYWORDS, 0, (LPARAM)keywords);

    sci(editor, SCI_STYLESETFORE, SCE_C_COMMENT, RGB(0, 128, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_COMMENTLINE, RGB(0, 128, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_COMMENTDOC, RGB(0, 128, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_COMMENTLINEDOC, RGB(0, 128, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_NUMBER, RGB(128, 0, 128));
    sci(editor, SCI_STYLESETFORE, SCE_C_WORD, RGB(0, 0, 255));
    sci(editor, SCI_STYLESETBOLD, SCE_C_WORD, FALSE);
    sci(editor, SCI_STYLESETFORE, SCE_C_STRING, RGB(192, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_CHARACTER, RGB(192, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_STRINGEOL, RGB(255, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_STRINGRAW, RGB(192, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_VERBATIM, RGB(192, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_TRIPLEVERBATIM, RGB(192, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_HASHQUOTEDSTRING, RGB(192, 0, 0));
    sci(editor, SCI_STYLESETFORE, SCE_C_REGEX, RGB(128, 0, 128));
    sci(editor, SCI_STYLESETFORE, SCE_C_ESCAPESEQUENCE, RGB(220, 0, 180));
    sci(editor, SCI_STYLESETFORE, SCE_C_OPERATOR, window_text);
    sci(editor, SCI_STYLESETFORE, STYLE_BRACELIGHT, RGB(0, 0, 0));
    sci(editor, SCI_STYLESETBACK, STYLE_BRACELIGHT, RGB(255, 255, 128));
    sci(editor, SCI_STYLESETFORE, STYLE_BRACEBAD, RGB(255, 255, 255));
    sci(editor, SCI_STYLESETBACK, STYLE_BRACEBAD, RGB(192, 0, 0));

    sci(editor, SCI_INDICSETSTYLE, INDICATOR_CLASS, INDIC_TEXTFORE);
    sci(editor, SCI_INDICSETFORE, INDICATOR_CLASS, assist_class);
    sci(editor, SCI_INDICSETSTYLE, INDICATOR_VARIABLE, INDIC_TEXTFORE);
    sci(editor, SCI_INDICSETFORE, INDICATOR_VARIABLE, assist_variable);
    sci(editor, SCI_INDICSETSTYLE, INDICATOR_METHOD, INDIC_TEXTFORE);
    sci(editor, SCI_INDICSETFORE, INDICATOR_METHOD, assist_method);
    sci(editor, SCI_INDICSETSTYLE, INDICATOR_MACRO, INDIC_TEXTFORE);
    sci(editor, SCI_INDICSETFORE, INDICATOR_MACRO, assist_macro);

    sci(editor, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    sci(editor, SCI_STYLESETBACK, STYLE_LINENUMBER, margin_background);
    sci(editor, SCI_STYLESETFORE, STYLE_LINENUMBER, window_text);
    sci(editor, SCI_SETMARGINBACKN, 0, margin_background);
    sci(editor, SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    sci(editor, SCI_SETMARGINWIDTHN, 1, 16);
    sci(editor, SCI_SETMARGINBACKN, 1, margin_background);
    sci(editor, SCI_SETMARGINMASKN, 1,
        (1u << MARKER_BREAKPOINT) | (1u << MARKER_EXECUTION) | (1u << MARKER_BOOKMARK));
    sci(editor, SCI_SETMARGINSENSITIVEN, 1, TRUE);

    sci(editor, SCI_MARKERDEFINE, MARKER_BREAKPOINT, SC_MARK_CIRCLE);
    sci(editor, SCI_MARKERSETFORE, MARKER_BREAKPOINT, RGB(255, 255, 255));
    sci(editor, SCI_MARKERSETBACK, MARKER_BREAKPOINT, RGB(190, 0, 0));
    sci(editor, SCI_MARKERDEFINE, MARKER_EXECUTION, SC_MARK_SHORTARROW);
    sci(editor, SCI_MARKERSETFORE, MARKER_EXECUTION, RGB(0, 0, 0));
    sci(editor, SCI_MARKERSETBACK, MARKER_EXECUTION, RGB(255, 255, 0));
    sci(editor, SCI_MARKERDEFINE, MARKER_BOOKMARK, SC_MARK_BOOKMARK);
    sci(editor, SCI_MARKERSETFORE, MARKER_BOOKMARK, RGB(255, 255, 255));
    sci(editor, SCI_MARKERSETBACK, MARKER_BOOKMARK, RGB(0, 90, 190));
    sci(editor, SCI_SETUSETABS, FALSE, 0);
    sci(editor, SCI_SETTABWIDTH, 4, 0);
    sci(editor, SCI_SETINDENT, 4, 0);
    sci(editor, SCI_SETTABINDENTS, TRUE, 0);
    sci(editor, SCI_SETBACKSPACEUNINDENTS, TRUE, 0);
    editor_update_line_number_margin(editor);
}

HWND editor_create(HWND parent, int control_id)
{
    HWND editor =
        CreateWindowExA(WS_EX_CLIENTEDGE, "Scintilla", "", WS_CHILD | WS_TABSTOP, 0, 0, 100, 100,
                        parent, (HMENU)(INT_PTR)control_id, GetModuleHandle(NULL), NULL);
    if (editor)
        configure_styles(editor);
    return editor;
}

int editor_open(HWND editor, const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *data;
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return 0;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    data[length] = '\0';
    sci(editor, SCI_SETTEXT, 0, (LPARAM)data);
    editor_update_line_number_margin(editor);
    sci(editor, SCI_EMPTYUNDOBUFFER, 0, 0);
    sci(editor, SCI_SETSAVEPOINT, 0, 0);
    editor_apply_semantic_colors(editor);
    if (path != current_path)
        snprintf(current_path, sizeof(current_path), "%s", path);
    free(data);
    return 1;
}

int editor_save_as(HWND editor, const char *path)
{
    FILE *file;
    char *data;
    LRESULT length;
    if (!path || !path[0])
        return 0;
    length = sci(editor, SCI_GETLENGTH, 0, 0);
    data = malloc((size_t)length + 1);
    if (!data)
        return 0;
    sci(editor, SCI_GETTEXT, (WPARAM)length + 1, (LPARAM)data);
    file = fopen(path, "wb");
    if (!file || fwrite(data, 1, (size_t)length, file) != (size_t)length) {
        if (file)
            fclose(file);
        free(data);
        return 0;
    }
    fclose(file);
    free(data);
    sci(editor, SCI_SETSAVEPOINT, 0, 0);
    snprintf(current_path, sizeof(current_path), "%s", path);
    return 1;
}

int editor_save(HWND editor)
{
    return editor_save_as(editor, current_path);
}

void editor_reset(HWND editor)
{
    sci(editor, SCI_SETTEXT, 0, (LPARAM) "");
    editor_update_line_number_margin(editor);
    sci(editor, SCI_EMPTYUNDOBUFFER, 0, 0);
    sci(editor, SCI_MARKERDELETEALL, MARKER_BREAKPOINT, 0);
    sci(editor, SCI_MARKERDELETEALL, MARKER_EXECUTION, 0);
    sci(editor, SCI_MARKERDELETEALL, MARKER_BOOKMARK, 0);
    current_path[0] = '\0';
}

int editor_is_modified(HWND editor)
{
    return (int)sci(editor, SCI_GETMODIFY, 0, 0);
}

int editor_current_line(HWND editor)
{
    LRESULT position = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    return (int)sci(editor, SCI_LINEFROMPOSITION, (WPARAM)position, 0);
}

int editor_selected_text(HWND editor, char *text, size_t capacity)
{
    LRESULT length;

    if (!text || capacity == 0)
        return 0;
    text[0] = '\0';
    length = sci(editor, SCI_GETSELTEXT, 0, 0);
    if (length <= 1 || (size_t)length > capacity)
        return 0;
    sci(editor, SCI_GETSELTEXT, 0, (LPARAM)text);
    return 1;
}

static int is_identifier_character(int character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || character == '$';
}

int editor_symbol_at_caret(HWND editor, char *symbol, size_t capacity)
{
    LRESULT start;
    LRESULT end;
    LRESULT position;
    size_t length;

    if (editor_selected_text(editor, symbol, capacity)) {
        length = strlen(symbol);
        if (!length)
            return 0;
        for (size_t index = 0; index < length; index++) {
            if (!is_identifier_character((unsigned char)symbol[index]))
                return 0;
        }
        return 1;
    }
    position = sci(editor, SCI_GETCURRENTPOS, 0, 0);
    start = position;
    end = position;
    while (start > 0 && is_identifier_character((int)sci(editor, SCI_GETCHARAT, start - 1, 0)))
        start--;
    while (is_identifier_character((int)sci(editor, SCI_GETCHARAT, end, 0)))
        end++;
    length = (size_t)(end - start);
    if (!length || length >= capacity)
        return 0;
    for (size_t index = 0; index < length; index++)
        symbol[index] = (char)sci(editor, SCI_GETCHARAT, start + (LRESULT)index, 0);
    symbol[length] = '\0';
    return 1;
}

void editor_goto_line(HWND editor, int line)
{
    if (line < 0)
        line = 0;
    sci(editor, SCI_GOTOLINE, (WPARAM)line, 0);
    sci(editor, SCI_SCROLLCARET, 0, 0);
    SetFocus(editor);
}

int editor_collect_breakpoints(HWND editor, int *lines, int capacity)
{
    int count = 0;
    int line_count = (int)sci(editor, SCI_GETLINECOUNT, 0, 0);
    for (int line = 0; line < line_count && count < capacity; line++) {
        int markers = (int)sci(editor, SCI_MARKERGET, line, 0);
        if (markers & (1 << MARKER_BREAKPOINT))
            lines[count++] = line + 1;
    }
    return count;
}

void editor_toggle_breakpoint(HWND editor, int line)
{
    int mask = (int)sci(editor, SCI_MARKERGET, line, 0);
    if (mask & (1 << MARKER_BREAKPOINT))
        sci(editor, SCI_MARKERDELETE, line, MARKER_BREAKPOINT);
    else
        sci(editor, SCI_MARKERADD, line, MARKER_BREAKPOINT);
}

void editor_set_execution_line(HWND editor, int line)
{
    editor_clear_execution_line(editor);
    sci(editor, SCI_MARKERADD, line, MARKER_EXECUTION);
    sci(editor, SCI_GOTOLINE, line, 0);
    sci(editor, SCI_ENSUREVISIBLE, line, 0);
}

void editor_clear_execution_line(HWND editor)
{
    sci(editor, SCI_MARKERDELETEALL, MARKER_EXECUTION, 0);
}

void editor_toggle_bookmark(HWND editor)
{
    int line = editor_current_line(editor);
    int mask = (int)sci(editor, SCI_MARKERGET, line, 0);

    if (mask & (1 << MARKER_BOOKMARK))
        sci(editor, SCI_MARKERDELETE, line, MARKER_BOOKMARK);
    else
        sci(editor, SCI_MARKERADD, line, MARKER_BOOKMARK);
}

int editor_next_bookmark(HWND editor, int forward)
{
    int current = editor_current_line(editor);
    int line_count = (int)sci(editor, SCI_GETLINECOUNT, 0, 0);
    int mask = 1 << MARKER_BOOKMARK;
    int line;

    if (forward) {
        line = (int)sci(editor, SCI_MARKERNEXT, current + 1, mask);
        if (line < 0)
            line = (int)sci(editor, SCI_MARKERNEXT, 0, mask);
    } else {
        line = (int)sci(editor, SCI_MARKERPREVIOUS, current - 1, mask);
        if (line < 0)
            line = (int)sci(editor, SCI_MARKERPREVIOUS, line_count - 1, mask);
    }
    if (line < 0)
        return 0;
    editor_goto_line(editor, line);
    return 1;
}

void editor_clear_bookmarks(HWND editor)
{
    sci(editor, SCI_MARKERDELETEALL, MARKER_BOOKMARK, 0);
}

const char *editor_path(void)
{
    return current_path;
}
