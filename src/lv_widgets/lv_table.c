/**
 * @file lv_table.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_table.h"
#if LV_USE_TABLE != 0

#include "../lv_core/lv_indev.h"
#include "../lv_misc/lv_assert.h"
#include "../lv_misc/lv_txt.h"
#include "../lv_misc/lv_txt_ap.h"
#include "../lv_misc/lv_math.h"
#include "../lv_misc/lv_printf.h"
#include "../lv_draw/lv_draw_label.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_table_class

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_table_constructor(lv_obj_t * obj, lv_obj_t * parent, const lv_obj_t * copy);
static void lv_table_destructor(lv_obj_t * obj);
static lv_draw_res_t lv_table_draw(lv_obj_t * obj, const lv_area_t * clip_area, lv_draw_mode_t mode);
static lv_res_t lv_table_signal(lv_obj_t * obj, lv_signal_t sign, void * param);
static lv_coord_t get_row_height(lv_obj_t * obj, uint16_t row_id, const lv_font_t * font,
                                 lv_coord_t letter_space, lv_coord_t line_space,
                                 lv_coord_t cell_left, lv_coord_t cell_right, lv_coord_t cell_top, lv_coord_t cell_bottom);
static void refr_size(lv_obj_t * obj);

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_table_class  = {
    .constructor_cb = lv_table_constructor,
    .destructor_cb = lv_table_destructor,
    .signal_cb = lv_table_signal,
    .draw_cb = lv_table_draw,
    .base_class = &lv_obj_class,
    .instance_size = sizeof(lv_table_t),
};
/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create a table object
 * @param par pointer to an object, it will be the parent of the new table
 * @param copy pointer to a table object, if not NULL then the new object will be copied from it
 * @return pointer to the created table
 */
lv_obj_t * lv_table_create(lv_obj_t * parent, const lv_obj_t * copy)
{
    return lv_obj_create_from_class(&lv_table_class, parent, copy);
}

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the value of a cell.
 * @param table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @param txt text to display in the cell. It will be copied and saved so this variable is not
 * required after this function call.
 */
void lv_table_set_cell_value(lv_obj_t * obj, uint16_t row, uint16_t col, const char * txt)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(txt);

    lv_table_t * table = (lv_table_t *) obj;

    /*Auto expand*/
    if(col >= table->col_cnt) lv_table_set_col_cnt(obj, col + 1);
    if(row >= table->row_cnt) lv_table_set_row_cnt(obj, row + 1);

    uint32_t cell = row * table->col_cnt + col;
    lv_table_cell_format_t format;

    /*Save the format byte*/
    if(table->cell_data[cell]) {
        format.format_byte = table->cell_data[cell][0];
    }
    /*Initialize the format byte*/
    else {
        format.s.right_merge = 0;
        format.s.crop        = 0;
    }

    #if LV_USE_ARABIC_PERSIAN_CHARS
    /*Get the size of the Arabic text and process it*/
    size_t len_ap = _lv_txt_ap_calc_bytes_cnt(txt);
    table->cell_data[cell] = lv_mem_realloc(table->cell_data[cell], len_ap + 1);
    LV_ASSERT_MALLOC(table->cell_data[cell]);
    if(table->cell_data[cell] == NULL) return;

    _lv_txt_ap_proc(txt, &table->cell_data[cell][1]);
#else
    table->cell_data[cell] = lv_mem_realloc(table->cell_data[cell], strlen(txt) + 2); /*+1: trailing '\0; +1: format byte*/
    LV_ASSERT_MALLOC(table->cell_data[cell]);

    strcpy(table->cell_data[cell] + 1, txt);  /*+1 to skip the format byte*/
#endif

    table->cell_data[cell][0] = format.format_byte;
    refr_size(obj) ;
}

/**
 * Set the value of a cell.  Memory will be allocated to store the text by the table.
 * @param table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @param fmt `printf`-like format
 */
void lv_table_set_cell_value_fmt(lv_obj_t * obj, uint16_t row, uint16_t col, const char * fmt, ...)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(fmt);

    lv_table_t * table = (lv_table_t *) obj;
    if(col >= table->col_cnt) {
        LV_LOG_WARN("lv_table_set_cell_value: invalid column");
        return;
    }

    /*Auto expand*/
    if(row >= table->row_cnt) {
        lv_table_set_row_cnt(obj, row + 1);
    }

    uint32_t cell = row * table->col_cnt + col;
    lv_table_cell_format_t format;

    /*Save the format byte*/
    if(table->cell_data[cell]) {
        format.format_byte = table->cell_data[cell][0];
    }
    /*Initialize the format byte*/
    else {
        format.s.right_merge = 0;
        format.s.crop        = 0;
    }

    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);

    /*Allocate space for the new text by using trick from C99 standard section 7.19.6.12 */
    uint32_t len = lv_vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

#if LV_USE_ARABIC_PERSIAN_CHARS
    /*Put together the text according to the format string*/
    char * raw_txt = lv_mem_buf_get(len + 1);
    LV_ASSERT_MALLOC(raw_txt);
    if(raw_txt == NULL) {
        va_end(ap2);
        return;
    }

    lv_vsnprintf(raw_txt, len + 1, fmt, ap2);

    /*Get the size of the Arabic text and process it*/
    size_t len_ap = _lv_txt_ap_calc_bytes_cnt(raw_txt);
    table->cell_data[cell] = lv_mem_realloc(table->cell_data[cell], len_ap + 1);
    LV_ASSERT_MALLOC(table->cell_data[cell]);
    if(table->cell_data[cell] == NULL) {
        va_end(ap2);
        return;
    }
    _lv_txt_ap_proc(raw_txt, &table->cell_data[cell][1]);

    lv_mem_buf_release(raw_txt);
#else
    table->cell_data[cell] = lv_mem_realloc(table->cell_data[cell], len + 2); /*+1: trailing '\0; +1: format byte*/
    LV_ASSERT_MALLOC(table->cell_data[cell]);
    if(table->cell_data[cell] == NULL) {
        va_end(ap2);
        return;
    }

    table->cell_data[cell][len + 1] = 0; /* Ensure NULL termination */

    lv_vsnprintf(&table->cell_data[cell][1], len + 1, fmt, ap2);
#endif

    va_end(ap2);

    table->cell_data[cell][0] = format.format_byte;
    refr_size(obj) ;
}

/**
 * Set the number of rows
 * @param table table pointer to a Table object
 * @param row_cnt number of rows
 */
void lv_table_set_row_cnt(lv_obj_t * obj, uint16_t row_cnt)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    uint16_t old_row_cnt = table->row_cnt;
    table->row_cnt         = row_cnt;

    table->row_h = lv_mem_realloc(table->row_h, table->row_cnt * sizeof(table->row_h[0]));
    LV_ASSERT_MALLOC(table->row_h);
    if(table->row_h == NULL) return;

    table->cell_data = lv_mem_realloc(table->cell_data, table->row_cnt * table->col_cnt * sizeof(char *));
    LV_ASSERT_MALLOC(table->cell_data);
    if(table->cell_data == NULL) return;

    /*Initialize the new fields*/
    if(old_row_cnt < row_cnt) {
        uint32_t old_cell_cnt = old_row_cnt * table->col_cnt;
        uint32_t new_cell_cnt = table->col_cnt * table->row_cnt;
        lv_memset_00(&table->cell_data[old_cell_cnt], (new_cell_cnt - old_cell_cnt) * sizeof(table->cell_data[0]));
    }

    refr_size(obj) ;
}

/**
 * Set the number of columns
 * @param table table pointer to a Table object
 * @param col_cnt number of columns.
 */
void lv_table_set_col_cnt(lv_obj_t * obj, uint16_t col_cnt)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    uint16_t old_col_cnt = table->col_cnt;
    table->col_cnt         = col_cnt;
    table->col_w = lv_mem_realloc(table->col_w, col_cnt * sizeof(table->row_h[0]));
    LV_ASSERT_MALLOC(table->col_w);
    if(table->col_w == NULL) return;

    char ** new_cell_data = lv_mem_alloc(table->row_cnt * table->col_cnt * sizeof(char *));
    LV_ASSERT_MALLOC(new_cell_data);
    if(new_cell_data == NULL) return;
    uint32_t new_cell_cnt = table->col_cnt * table->row_cnt;
    lv_memset_00(new_cell_data, new_cell_cnt * sizeof(table->cell_data[0]));

    /*Initialize the new fields*/
    if(old_col_cnt < col_cnt) {
        uint32_t col;
        for(col = old_col_cnt; col < col_cnt; col++) {
            table->col_w[col] = LV_DPI_DEF;
        }
    }

    /*The new column(s) messes up the mapping of `cell_data`*/
    uint32_t old_col_start;
    uint32_t new_col_start;
    uint32_t min_col_cnt = LV_MIN(old_col_cnt, col_cnt);
    uint32_t row;
    for(row = 0; row < table->row_cnt; row++) {
        old_col_start = row * old_col_cnt;
        new_col_start = row * col_cnt;

        lv_memcpy_small(&new_cell_data[new_col_start], &table->cell_data[old_col_start], sizeof(new_cell_data[0]) * min_col_cnt);
    }

    lv_mem_free(table->cell_data);
    table->cell_data = new_cell_data;


    refr_size(obj) ;
}

/**
 * Set the width of a column
 * @param table table pointer to a Table object
 * @param col_id id of the column [0 .. LV_TABLE_COL_MAX -1]
 * @param w width of the column
 */
void lv_table_set_col_width(lv_obj_t * obj, uint16_t col_id, lv_coord_t w)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;

    /*Auto expand*/
    if(col_id >= table->col_cnt) lv_table_set_col_cnt(obj, col_id + 1);

    table->col_w[col_id] = w;
    refr_size(obj) ;
}

/**
 * Set the cell crop. (Don't adjust the height of the cell according to its content)
 * @param table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @param crop true: crop the cell content; false: set the cell height to the content.
 */
void lv_table_set_cell_crop(lv_obj_t * obj, uint16_t row, uint16_t col, bool crop)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;

    /*Auto expand*/
    if(col >= table->col_cnt) lv_table_set_col_cnt(obj, col + 1);
    if(row >= table->row_cnt) lv_table_set_row_cnt(obj, row + 1);

    uint32_t cell = row * table->col_cnt + col;

    if(table->cell_data[cell] == NULL) {
        table->cell_data[cell]    = lv_mem_alloc(2); /*+1: trailing '\0; +1: format byte*/
        LV_ASSERT_MALLOC(table->cell_data[cell]);
        if(table->cell_data[cell] == NULL) return;

        table->cell_data[cell][0] = 0;
        table->cell_data[cell][1] = '\0';
    }

    lv_table_cell_format_t format;
    format.format_byte      = table->cell_data[cell][0];
    format.s.crop           = crop;
    table->cell_data[cell][0] = format.format_byte;
}

/**
 * Merge a cell with the right neighbor. The value of the cell to the right won't be displayed.
 * @param table table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @param en true: merge right; false: don't merge right
 */
void lv_table_set_cell_merge_right(lv_obj_t * obj, uint16_t row, uint16_t col, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;

    /*Auto expand*/
    if(col >= table->col_cnt) lv_table_set_col_cnt(obj, col + 1);
    if(row >= table->row_cnt) lv_table_set_row_cnt(obj, row + 1);

    uint32_t cell = row * table->col_cnt + col;

    if(table->cell_data[cell] == NULL) {
        table->cell_data[cell]    = lv_mem_alloc(2); /*+1: trailing '\0; +1: format byte*/
        LV_ASSERT_MALLOC(table->cell_data[cell]);
        if(table->cell_data[cell] == NULL) return;

        table->cell_data[cell][0] = 0;
        table->cell_data[cell][1] = '\0';
    }

    lv_table_cell_format_t format;
    format.format_byte      = table->cell_data[cell][0];
    format.s.right_merge    = en ? 1 : 0;
    table->cell_data[cell][0] = format.format_byte;
    refr_size(obj) ;
}


/*=====================
 * Getter functions
 *====================*/

/**
 * Get the value of a cell.
 * @param table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @return text in the cell
 */
const char * lv_table_get_cell_value(lv_obj_t * obj, uint16_t row, uint16_t col)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    if(row >= table->row_cnt || col >= table->col_cnt) {
        LV_LOG_WARN("lv_table_set_cell_value: invalid row or column");
        return "";
    }
    uint32_t cell = row * table->col_cnt + col;

    if(table->cell_data[cell] == NULL) return "";

    return &table->cell_data[cell][1]; /*Skip the format byte*/
}

/**
 * Get the number of rows.
 * @param table table pointer to a Table object
 * @return number of rows.
 */
uint16_t lv_table_get_row_cnt(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    return table->row_cnt;
}

/**
 * Get the number of columns.
 * @param table table pointer to a Table object
 * @return number of columns.
 */
uint16_t lv_table_get_col_cnt(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    return table->col_cnt;
}

/**
 * Get the width of a column
 * @param table table pointer to a Table object
 * @param col_id id of the column [0 .. LV_TABLE_COL_MAX -1]
 * @return width of the column
 */
lv_coord_t lv_table_get_col_width(lv_obj_t * obj, uint16_t col_id)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;

    if(col_id >= table->col_cnt) {
        LV_LOG_WARN("lv_table_set_col_width: too big 'col_id'. Must be < LV_TABLE_COL_MAX.");
        return 0;
    }

    return table->col_w[col_id];
}

/**
 * Get the crop property of a cell
 * @param table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @return true: text crop enabled; false: disabled
 */
bool lv_table_get_cell_crop(lv_obj_t * obj, uint16_t row, uint16_t col)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    if(row >= table->row_cnt || col >= table->col_cnt) {
        LV_LOG_WARN("lv_table_get_cell_crop: invalid row or column");
        return false; /*Just return with something*/
    }
    uint32_t cell = row * table->col_cnt + col;

    if(table->cell_data[cell] == NULL)
        return false; /*Just return with something*/
    else {
        lv_table_cell_format_t format;
        format.format_byte = table->cell_data[cell][0];
        return format.s.crop;
    }
}

/**
 * Get the cell merge attribute.
 * @param table table pointer to a Table object
 * @param row id of the row [0 .. row_cnt -1]
 * @param col id of the column [0 .. col_cnt -1]
 * @return true: merge right; false: don't merge right
 */
bool lv_table_get_cell_merge_right(lv_obj_t * obj, uint16_t row, uint16_t col)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_table_t * table = (lv_table_t *) obj;
    if(row >= table->row_cnt || col >= table->col_cnt) {
        LV_LOG_WARN("lv_table_get_cell_merge_right: invalid row or column");
        return false;
    }

    uint32_t cell = row * table->col_cnt + col;

    if(table->cell_data[cell] == NULL)
        return false;
    else {
        lv_table_cell_format_t format;
        format.format_byte = table->cell_data[cell][0];
        return format.s.right_merge ? true : false;
    }
}

/**
 * Get the last pressed or being pressed cell
 * @param table pointer to a table object
 * @param row pointer to variable to store the pressed row
 * @param col pointer to variable to store the pressed column
 * @return LV_RES_OK: a valid pressed cell was found, LV_RES_INV: no valid cell is pressed
 */
lv_res_t lv_table_get_pressed_cell(lv_obj_t * obj, uint16_t * row, uint16_t * col)
{
    lv_table_t * table = (lv_table_t *) obj;

    lv_indev_type_t type = lv_indev_get_type(lv_indev_get_act());
    if(type != LV_INDEV_TYPE_POINTER && type != LV_INDEV_TYPE_BUTTON) {
        if(col) *col = 0xFFFF;
        if(row) *row = 0xFFFF;
        return LV_RES_INV;
    }

    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);

    lv_coord_t tmp;
    if(col) {
        lv_coord_t x = p.x;
        x -= obj->coords.x1;
        x -= lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
        *col = 0;
        tmp = 0;
        for(*col = 0; *col < table->col_cnt; (*col)++) {
            tmp += table->col_w[*col];
            if(x < tmp) break;
        }
    }

    if(row) {
        lv_coord_t y = p.y;
        y -= obj->coords.y1;
        y -= lv_obj_get_style_pad_top(obj, LV_PART_MAIN);

        *row = 0;
        tmp = 0;

        for(*row = 0; *row < table->row_cnt; (*row)++) {
            tmp += table->row_h[*row];
            if(y < tmp) break;
        }
    }

    return LV_RES_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_table_constructor(lv_obj_t * obj, lv_obj_t * parent, const lv_obj_t * copy)
{
    LV_LOG_TRACE("lv_table create started");

    lv_table_t * table = (lv_table_t *) obj;

    /*Initialize the allocated 'ext' */
    table->cell_data     = NULL;
    table->col_cnt       = 0;
    table->row_cnt       = 0;
    table->row_h         = NULL;
    table->col_w         = NULL;

    /*Init the new table table*/
    if(copy == NULL) {
        table->col_cnt = 1;
        table->row_cnt = 1;
        table->col_w = lv_mem_alloc(table->col_cnt * sizeof(table->col_w[0]));
        table->row_h = lv_mem_alloc(table->row_cnt * sizeof(table->row_h[0]));
        table->col_w[0] = LV_DPI_DEF;
        table->row_h[0] = LV_DPI_DEF;
        table->cell_data = lv_mem_realloc(table->cell_data, table->row_cnt * table->col_cnt * sizeof(char *));
        table->cell_data[0] = NULL;

        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    /*Copy an existing table*/
    else {
        lv_table_t * copy_table = (lv_table_t *) copy;
        lv_table_set_row_cnt(obj, copy_table->row_cnt);
        lv_table_set_col_cnt(obj, copy_table->col_cnt);
    }
   LV_LOG_INFO("table created");
}

static void lv_table_destructor(lv_obj_t * obj)
{
//    /*Free the cell texts*/
//           uint16_t i;
//           for(i = 0; i < table->col_cnt * table->row_cnt; i++) {
//               if(table->cell_data[i]) {
//                   lv_mem_free(table->cell_data[i]);
//                   table->cell_data[i] = NULL;
//               }
//           }
//
//           if(table->cell_data) lv_mem_free(table->cell_data);
//           if(table->row_h) lv_mem_free(table->row_h);
//
//           for(i = 0; i < LV_TABLE_CELL_STYLE_CNT; i++) {
//               _lv_obj_reset_style_list_no_refr(obj, LV_TABLE_PART_CELL1 + i);
//           }
}

static lv_draw_res_t lv_table_draw(lv_obj_t * obj, const lv_area_t * clip_area, lv_draw_mode_t mode)
{
    /*Return false if the object is not covers the mask_p area*/
    if(mode == LV_DRAW_MODE_COVER_CHECK) {
        return lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);
    }
    /*Draw the object*/
    else if(mode == LV_DRAW_MODE_MAIN_DRAW) {
        /*Draw the background*/
        lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);

        lv_table_t * table = (lv_table_t *) obj;

        lv_point_t txt_size;
        lv_area_t cell_area;
        lv_area_t txt_area;
        lv_text_flag_t txt_flags;

        lv_coord_t bg_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
        lv_coord_t bg_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
        lv_coord_t bg_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
        lv_coord_t bg_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);

        lv_coord_t cell_left = lv_obj_get_style_pad_left(obj, LV_PART_ITEMS);
        lv_coord_t cell_right = lv_obj_get_style_pad_right(obj, LV_PART_ITEMS);
        lv_coord_t cell_top = lv_obj_get_style_pad_top(obj, LV_PART_ITEMS);
        lv_coord_t cell_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_ITEMS);

        lv_draw_rect_dsc_t rect_dsc_base;
        lv_draw_rect_dsc_t rect_dsc_act; /*Passed to the draw_cb to modify it*/
        lv_draw_rect_dsc_init(&rect_dsc_base);
        lv_obj_init_draw_rect_dsc(obj, LV_PART_ITEMS, &rect_dsc_base);

        lv_draw_label_dsc_t label_dsc_base;
        lv_draw_label_dsc_t label_dsc_act;  /*Passed to the draw_cb to modify it*/
        lv_draw_label_dsc_init(&label_dsc_base);
        lv_obj_init_draw_label_dsc(obj, LV_PART_ITEMS, &label_dsc_base);

        uint16_t col;
        uint16_t row;
        uint16_t cell = 0;

        cell_area.y2 = obj->coords.y1 + bg_top - 1 - lv_obj_get_scroll_y(obj) ;
        lv_coord_t scroll_x = lv_obj_get_scroll_x(obj) ;
        bool rtl = lv_obj_get_base_dir(obj)  == LV_BIDI_DIR_RTL ? true : false;

        /*Handle custom drawer*/
        lv_obj_draw_hook_dsc_t hook_dsc;
        lv_obj_draw_hook_dsc_init(&hook_dsc, clip_area);
        hook_dsc.part = LV_PART_ITEMS;
        hook_dsc.rect_dsc = &rect_dsc_act;
        hook_dsc.label_dsc = &label_dsc_act;

        for(row = 0; row < table->row_cnt; row++) {
            lv_coord_t h_row = table->row_h[row];

            cell_area.y1 = cell_area.y2 + 1;
            cell_area.y2 = cell_area.y1 + h_row - 1;

            if(cell_area.y1 > clip_area->y2) return LV_DRAW_RES_OK;

            if(rtl) cell_area.x1 = obj->coords.x2 - bg_right - 1 - scroll_x;
            else cell_area.x2 = obj->coords.x1 + bg_left - 1 - scroll_x;

            for(col = 0; col < table->col_cnt; col++) {
                lv_table_cell_format_t format;
                if(table->cell_data[cell]) {
                    format.format_byte = table->cell_data[cell][0];
                }
                else {
                    format.s.right_merge = 0;
                    format.s.crop        = 1;
                }

                if(rtl) {
                    cell_area.x2 = cell_area.x1 - 1;
                    cell_area.x1 = cell_area.x2 - table->col_w[col] + 1;
                }
                else {
                    cell_area.x1 = cell_area.x2 + 1;
                    cell_area.x2 = cell_area.x1 + table->col_w[col] - 1;
                }

                uint16_t col_merge = 0;
                for(col_merge = 0; col_merge + col < table->col_cnt - 1; col_merge++) {
                    if(table->cell_data[cell + col_merge]) {
                        format.format_byte = table->cell_data[cell + col_merge][0];
                        if(format.s.right_merge)
                            if(rtl) cell_area.x1 -= table->col_w[col + col_merge + 1];
                            else cell_area.x2 += table->col_w[col + col_merge + 1];
                        else {
                            break;
                        }
                    }
                    else {
                        break;
                    }
                }

                if(cell_area.y2 < clip_area->y1) {
                    cell += col_merge + 1;
                    col += col_merge;
                    continue;
                }

                /*Expand the cell area with a half border to avoid drawing 2 borders next to each other*/
                lv_area_t cell_area_border;
                lv_area_copy(&cell_area_border, &cell_area);
                if((rect_dsc_base.border_side & LV_BORDER_SIDE_LEFT) && cell_area_border.x1 > obj->coords.x1 + bg_left) {
                    cell_area_border.x1 -= rect_dsc_base.border_width / 2;
                }
                if((rect_dsc_base.border_side & LV_BORDER_SIDE_TOP) && cell_area_border.y1 > obj->coords.y1 + bg_top) {
                    cell_area_border.y1 -= rect_dsc_base.border_width / 2;
                }
                if((rect_dsc_base.border_side & LV_BORDER_SIDE_RIGHT) && cell_area_border.x2 < obj->coords.x2 - bg_right - 1) {
                    cell_area_border.x2 += rect_dsc_base.border_width / 2 + (rect_dsc_base.border_width & 0x1);
                }
                if((rect_dsc_base.border_side & LV_BORDER_SIDE_BOTTOM) &&
                   cell_area_border.y2 < obj->coords.y2 - bg_bottom - 1) {
                   cell_area_border.y2 += rect_dsc_base.border_width / 2 + (rect_dsc_base.border_width & 0x1);
                }

                lv_memcpy(&rect_dsc_act, &rect_dsc_base, sizeof(lv_draw_rect_dsc_t));
                lv_memcpy(&label_dsc_act, &label_dsc_base, sizeof(lv_draw_label_dsc_t));
                hook_dsc.draw_area = &cell_area_border;
                hook_dsc.id = row * table->col_cnt + col;
                lv_event_send(obj, LV_EVENT_DRAW_PART_BEGIN, &hook_dsc);

                lv_draw_rect(&cell_area_border, clip_area, &rect_dsc_act);

                if(table->cell_data[cell]) {
                    txt_area.x1 = cell_area.x1 + cell_left;
                    txt_area.x2 = cell_area.x2 - cell_right;
                    txt_area.y1 = cell_area.y1 + cell_top;
                    txt_area.y2 = cell_area.y2 - cell_bottom;

                    /*Align the content to the middle if not cropped*/
                    if(format.s.crop == 0) txt_flags = LV_TEXT_FLAG_NONE;
                    else txt_flags = LV_TEXT_FLAG_EXPAND;

                    lv_txt_get_size(&txt_size, table->cell_data[cell] + 1, label_dsc_base.font,
                                     label_dsc_act.letter_space, label_dsc_act.line_space,
                                     lv_area_get_width(&txt_area), txt_flags);

                    /*Align the content to the middle if not cropped*/
                    if(format.s.crop == 0) {
                        txt_area.y1 = cell_area.y1 + h_row / 2 - txt_size.y / 2;
                        txt_area.y2 = cell_area.y1 + h_row / 2 + txt_size.y / 2;
                    }

                    lv_area_t label_mask;
                    bool label_mask_ok;
                    label_mask_ok = _lv_area_intersect(&label_mask, clip_area, &cell_area);
                    if(label_mask_ok) {
                        lv_draw_label(&txt_area, &label_mask, &label_dsc_act, table->cell_data[cell] + 1, NULL);
                    }
                }

                lv_event_send(obj, LV_EVENT_DRAW_PART_END, &hook_dsc);

                cell += col_merge + 1;
                col += col_merge;
            }
        }
    }
    /*Post draw when the children are drawn*/
    else if(mode == LV_DRAW_MODE_POST_DRAW) {
        lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);
    }

    return LV_DRAW_RES_OK;
}

static lv_res_t lv_table_signal(lv_obj_t * obj, lv_signal_t sign, void * param)
{
    lv_res_t res;

    /* Include the ancient signal function */
    res = lv_obj_signal_base(MY_CLASS, obj, sign, param);
    if(res != LV_RES_OK) return res;

    lv_table_t * table = (lv_table_t *) obj;

    if(sign == LV_SIGNAL_STYLE_CHG) {
        refr_size(obj);
    }
    else if(sign == LV_SIGNAL_GET_SELF_SIZE) {
        lv_point_t * p = param;
        uint32_t i;
        lv_coord_t w = 0;
        for(i = 0; i < table->col_cnt; i++) w += table->col_w[i];

        lv_coord_t h = 0;
        for(i = 0; i < table->row_cnt; i++) h += table->row_h[i];

        p->x = w;
        p->y = h;
    }

    return res;
}


static void refr_size(lv_obj_t * obj)
{
    lv_table_t * table = (lv_table_t *) obj;

    uint32_t i;

    lv_coord_t cell_left = lv_obj_get_style_pad_left(obj, LV_PART_ITEMS);
    lv_coord_t cell_right = lv_obj_get_style_pad_right(obj, LV_PART_ITEMS);
    lv_coord_t cell_top = lv_obj_get_style_pad_top(obj, LV_PART_ITEMS);
    lv_coord_t cell_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_ITEMS);

    lv_coord_t letter_space = lv_obj_get_style_text_letter_space(obj, LV_PART_ITEMS);
    lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_ITEMS);
    const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_ITEMS);

    for(i = 0; i < table->row_cnt; i++) {
        table->row_h[i] = get_row_height(obj, i, font, letter_space, line_space,
                                       cell_left, cell_right, cell_top, cell_bottom);
    }

    lv_obj_handle_self_size_chg(obj) ;
}

static lv_coord_t get_row_height(lv_obj_t * obj, uint16_t row_id, const lv_font_t * font,
                                 lv_coord_t letter_space, lv_coord_t line_space,
                                 lv_coord_t cell_left, lv_coord_t cell_right, lv_coord_t cell_top, lv_coord_t cell_bottom)
{
    lv_table_t * table = (lv_table_t *) obj;
    lv_point_t txt_size;
    lv_coord_t txt_w;

    uint16_t row_start = row_id * table->col_cnt;
    uint16_t cell;
    uint16_t col;
    lv_coord_t h_max = lv_font_get_line_height(font) + cell_top + cell_bottom;

    for(cell = row_start, col = 0; cell < row_start + table->col_cnt; cell++, col++) {
        if(table->cell_data[cell] != NULL) {
            txt_w              = table->col_w[col];
            uint16_t col_merge = 0;
            for(col_merge = 0; col_merge + col < table->col_cnt - 1; col_merge++) {

                if(table->cell_data[cell + col_merge] != NULL) {
                    lv_table_cell_format_t format;
                    format.format_byte = table->cell_data[cell + col_merge][0];
                    if(format.s.right_merge)
                        txt_w += table->col_w[col + col_merge + 1];
                    else
                        break;
                }
                else {
                    break;
                }
            }

            lv_table_cell_format_t format;
            format.format_byte = table->cell_data[cell][0];

            /*With text crop assume 1 line*/
            if(format.s.crop) {
                h_max = LV_MAX(lv_font_get_line_height(font) + cell_top + cell_bottom,
                                    h_max);
            }
            /*Without text crop calculate the height of the text in the cell*/
            else {
                txt_w -= cell_left + cell_right;

                lv_txt_get_size(&txt_size, table->cell_data[cell] + 1, font,
                                 letter_space, line_space, txt_w, LV_TEXT_FLAG_NONE);

                h_max = LV_MAX(txt_size.y + cell_top + cell_bottom, h_max);
                cell += col_merge;
                col += col_merge;
            }
        }
    }

    return h_max;
}

#endif
