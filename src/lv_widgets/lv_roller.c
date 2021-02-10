/**
 * @file lv_roller.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_roller.h"
#if LV_USE_ROLLER != 0

#include "../lv_misc/lv_assert.h"
#include "../lv_draw/lv_draw.h"
#include "../lv_core/lv_group.h"
#include "../lv_core/lv_indev.h"
#include "../lv_core/lv_indev_scroll.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_roller_class
#define MY_CLASS_LABEL &lv_roller_label_class

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_roller_constructor(lv_obj_t * obj, lv_obj_t * parent, const lv_obj_t * copy);
static lv_draw_res_t lv_roller_draw(lv_obj_t * obj, const lv_area_t * clip_area, lv_draw_mode_t mode);
static lv_draw_res_t lv_roller_label_draw(lv_obj_t * label_obj, const lv_area_t * clip_area, lv_draw_mode_t mode);
static lv_res_t lv_roller_signal(lv_obj_t * obj, lv_signal_t sign, void * param);
static lv_res_t lv_roller_label_signal(lv_obj_t * label, lv_signal_t sign, void * param);
static void refr_position(lv_obj_t * obj, lv_anim_enable_t animen);
static lv_res_t release_handler(lv_obj_t * obj);
static void inf_normalize(lv_obj_t * obj_scrl);
static lv_obj_t * get_label(const lv_obj_t * obj);
static lv_coord_t get_selected_label_width(const lv_obj_t * obj);
static void scroll_anim_ready_cb(lv_anim_t * a);

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_roller_class = {
        .constructor_cb = lv_roller_constructor,
        .signal_cb = lv_roller_signal,
        .draw_cb = lv_roller_draw,
        .instance_size = sizeof(lv_roller_t),
        .editable = LV_OBJ_CLASS_EDITABLE_TRUE,
        .base_class = &lv_obj_class
};

const lv_obj_class_t lv_roller_label_class  = {
        .signal_cb = lv_roller_label_signal,
        .draw_cb = lv_roller_label_draw,
        .instance_size = sizeof(lv_label_t),
        .base_class = &lv_label_class
    };

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create a roller object
 * @param par pointer to an object, it will be the parent of the new roller
 * @param copy pointer to a roller object, if not NULL then the new object will be copied from it
 * @return pointer to the created roller
 */
lv_obj_t * lv_roller_create(lv_obj_t * parent, const lv_obj_t * copy)
{
    return lv_obj_create_from_class(&lv_roller_class, parent, copy);
}

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the options on a roller
 * @param roller pointer to roller object
 * @param options a string with '\n' separated options. E.g. "One\nTwo\nThree"
 * @param mode `LV_ROLLER_MODE_NORMAL` or `LV_ROLLER_MODE_INFINITE`
 */
void lv_roller_set_options(lv_obj_t * obj, const char * options, lv_roller_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(options);

    lv_roller_t * roller = (lv_roller_t*)obj;
    lv_obj_t * label = get_label(obj);

    roller->sel_opt_id     = 0;
    roller->sel_opt_id_ori = 0;

    /*Count the '\n'-s to determine the number of options*/
    roller->option_cnt = 0;
    uint32_t cnt;
    for(cnt = 0; options[cnt] != '\0'; cnt++) {
        if(options[cnt] == '\n') roller->option_cnt++;
    }
    roller->option_cnt++; /*Last option has no `\n`*/

    if(mode == LV_ROLLER_MODE_NORMAL) {
        roller->mode = LV_ROLLER_MODE_NORMAL;
        lv_label_set_text(label, options);
    }
    else {
        roller->mode = LV_ROLLER_MODE_INFINITE;

        size_t opt_len = strlen(options) + 1; /*+1 to add '\n' after option lists*/
        char * opt_extra = lv_mem_buf_get(opt_len * LV_ROLLER_INF_PAGES);
        uint8_t i;
        for(i = 0; i < LV_ROLLER_INF_PAGES; i++) {
            strcpy(&opt_extra[opt_len * i], options);
            opt_extra[opt_len * (i + 1) - 1] = '\n';
        }
        opt_extra[opt_len * LV_ROLLER_INF_PAGES - 1] = '\0';
        lv_label_set_text(label, opt_extra);
        lv_mem_buf_release(opt_extra);

        roller->sel_opt_id     = ((LV_ROLLER_INF_PAGES / 2) + 0) * roller->option_cnt;

        roller->option_cnt = roller->option_cnt * LV_ROLLER_INF_PAGES;
        inf_normalize(obj);
    }

    roller->sel_opt_id_ori = roller->sel_opt_id;

    /*If the selected text has larger font the label needs some extra draw padding to draw it.*/
    lv_obj_refresh_ext_draw_size(label);

}

/**
 * Set the selected option
 * @param roller pointer to a roller object
 * @param sel_opt id of the selected option (0 ... number of option - 1);
 * @param anim_en LV_ANIM_ON: set with animation; LV_ANOM_OFF set immediately
 */
void lv_roller_set_selected(lv_obj_t * obj, uint16_t sel_opt, lv_anim_enable_t anim)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    /* Set the value even if it's the same as the current value because
     * if moving to the next option with an animation which was just deleted in the PRESS signal
     * nothing will continue the animation. */

    lv_roller_t * roller = (lv_roller_t*)obj;

    /*In infinite mode interpret the new ID relative to the currently visible "page"*/
    if(roller->mode == LV_ROLLER_MODE_INFINITE) {
        int32_t sel_opt_signed = sel_opt;
        uint16_t page = roller->sel_opt_id / LV_ROLLER_INF_PAGES;

        /* `sel_opt` should be less than the number of options set by the user.
         * If it's more then probably it's a reference from not the first page
         * so normalize `sel_opt` */
        if(page != 0) {
            sel_opt_signed -= page * LV_ROLLER_INF_PAGES;
        }

        sel_opt = page * LV_ROLLER_INF_PAGES + sel_opt_signed;
    }

    roller->sel_opt_id     = sel_opt < roller->option_cnt ? sel_opt : roller->option_cnt - 1;
    roller->sel_opt_id_ori = roller->sel_opt_id;

    refr_position(obj, anim);
}

/**
 * Set the height to show the given number of rows (options)
 * @param roller pointer to a roller object
 * @param row_cnt number of desired visible rows
 */
void lv_roller_set_visible_row_count(lv_obj_t * obj, uint8_t row_cnt)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
    lv_obj_set_height(obj, (lv_font_get_line_height(font) + line_space) * row_cnt);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Get the id of the selected option
 * @param roller pointer to a roller object
 * @return id of the selected option (0 ... number of option - 1);
 */
uint16_t lv_roller_get_selected(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_roller_t * roller = (lv_roller_t*)obj;
    if(roller->mode == LV_ROLLER_MODE_INFINITE) {
        uint16_t real_id_cnt = roller->option_cnt / LV_ROLLER_INF_PAGES;
        return roller->sel_opt_id % real_id_cnt;
    }
    else {
        return roller->sel_opt_id;
    }
}

/**
 * Get the current selected option as a string
 * @param ddlist pointer to ddlist object
 * @param buf pointer to an array to store the string
 * @param buf_size size of `buf` in bytes. 0: to ignore it.
 */
void lv_roller_get_selected_str(const lv_obj_t * obj, char * buf, uint32_t buf_size)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_roller_t * roller = (lv_roller_t*)obj;
    lv_obj_t * label = get_label(obj);
    uint32_t i;
    uint16_t line        = 0;
    const char * opt_txt = lv_label_get_text(label);
    size_t txt_len     = strlen(opt_txt);

    for(i = 0; i < txt_len && line != roller->sel_opt_id; i++) {
        if(opt_txt[i] == '\n') line++;
    }

    uint32_t c;
    for(c = 0; i < txt_len && opt_txt[i] != '\n'; c++, i++) {
        if(buf_size && c >= buf_size - 1) {
            LV_LOG_WARN("lv_dropdown_get_selected_str: the buffer was too small")
            break;
        }
        buf[c] = opt_txt[i];
    }

    buf[c] = '\0';
}


/**
 * Get the options of a roller
 * @param roller pointer to roller object
 * @return the options separated by '\n'-s (E.g. "Option1\nOption2\nOption3")
 */
const char * lv_roller_get_options(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    return lv_label_get_text(get_label(obj));
}


/**
 * Get the total number of options
 * @param roller pointer to a roller object
 * @return the total number of options
 */
uint16_t lv_roller_get_option_cnt(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_roller_t * roller = (lv_roller_t*)obj;
    if(roller->mode == LV_ROLLER_MODE_INFINITE) {
        return roller->option_cnt / LV_ROLLER_INF_PAGES;
    }
    else {
        return roller->option_cnt;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


static void lv_roller_constructor(lv_obj_t * obj, lv_obj_t * parent, const lv_obj_t * copy)
{

    lv_roller_t * roller = (lv_roller_t*)obj;

    roller->mode = LV_ROLLER_MODE_NORMAL;
    roller->option_cnt = 0;
    roller->sel_opt_id = 0;
    roller->sel_opt_id_ori = 0;

    /*Init the new roller roller*/
    if(copy == NULL) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_set_width(obj, LV_SIZE_CONTENT);

        lv_obj_create_from_class(&lv_roller_label_class, obj, NULL);
        lv_roller_set_options(obj, "Option 1\nOption 2\nOption 3\nOption 4\nOption 5", LV_ROLLER_MODE_NORMAL);
        lv_obj_set_height(obj, LV_DPI_DEF);
    }
    else {
        lv_obj_create_from_class(&lv_roller_label_class, obj, NULL);
        lv_roller_t * copy_roller = (lv_roller_t *) copy;
        roller->mode = copy_roller->mode;
        roller->option_cnt = copy_roller->option_cnt;
        roller->sel_opt_id = copy_roller->sel_opt_id;
        roller->sel_opt_id_ori = copy_roller->sel_opt_id;
    }

    LV_LOG_INFO("roller created");

}

static lv_draw_res_t lv_roller_draw(lv_obj_t * obj, const lv_area_t * clip_area, lv_draw_mode_t mode)
{
    if(mode == LV_DRAW_MODE_COVER_CHECK) {
        return lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);
    }
    /*Draw the object*/
    else if(mode == LV_DRAW_MODE_MAIN_DRAW) {
        lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);

        /*Draw the selected rectangle*/
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
        lv_coord_t font_h        = lv_font_get_line_height(font);
        lv_area_t rect_area;
        rect_area.y1 = obj->coords.y1 + (lv_obj_get_height(obj) - font_h - line_space) / 2;
        rect_area.y2 = rect_area.y1 + font_h + line_space - 1;
        lv_area_t roller_coords;
        lv_obj_get_coords(obj, &roller_coords);

        rect_area.x1 = roller_coords.x1;
        rect_area.x2 = roller_coords.x2;

        lv_draw_rect_dsc_t sel_dsc;
        lv_draw_rect_dsc_init(&sel_dsc);
        lv_obj_init_draw_rect_dsc(obj, LV_PART_SELECTED, &sel_dsc);
        lv_draw_rect(&rect_area, clip_area, &sel_dsc);
    }
    /*Post draw when the children are drawn*/
    else if(mode == LV_DRAW_MODE_POST_DRAW) {
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        lv_obj_init_draw_label_dsc(obj, LV_PART_SELECTED, &label_dsc);

        lv_coord_t bg_font_h = lv_font_get_line_height(lv_obj_get_style_text_font(obj, LV_PART_MAIN));

        /*Redraw the text on the selected area*/
        lv_area_t rect_area;
        rect_area.y1 = obj->coords.y1 + (lv_obj_get_height(obj) - bg_font_h - label_dsc.line_space) / 2;
        rect_area.y2 = rect_area.y1 + bg_font_h + label_dsc.line_space - 1;
        rect_area.x1 = obj->coords.x1;
        rect_area.x2 = obj->coords.x2;
        lv_area_t mask_sel;
        bool area_ok;
        area_ok = _lv_area_intersect(&mask_sel, clip_area, &rect_area);
        if(area_ok) {
            lv_obj_t * label = get_label(obj);

            /*Get the size of the "selected text"*/
            lv_point_t res_p;
            lv_txt_get_size(&res_p, lv_label_get_text(label), label_dsc.font, label_dsc.letter_space, label_dsc.line_space,
                             lv_obj_get_width(obj), LV_TEXT_FLAG_EXPAND);

            /*Move the selected label proportionally with the background label*/
            lv_coord_t roller_h = lv_obj_get_height(obj);
            int32_t label_y_prop = label->coords.y1 - (roller_h / 2 +
                    obj->coords.y1); /*label offset from the middle line of the roller*/
            label_y_prop = (label_y_prop << 14) / lv_obj_get_height(
                               label); /*Proportional position from the middle line (upscaled)*/

            /*Apply a correction with different line heights*/
            const lv_font_t * normal_label_font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
            lv_coord_t corr = (label_dsc.font->line_height - normal_label_font->line_height) / 2;

            /*Apply the proportional position to the selected text*/
            res_p.y -= corr;
            int32_t label_sel_y = roller_h / 2 + obj->coords.y1;
            label_sel_y += (label_y_prop * res_p.y) >> 14;
            label_sel_y -= corr;

            /*Draw the selected text*/
            lv_area_t label_sel_area;
            label_sel_area.x1 = label->coords.x1;
            label_sel_area.y1 = label_sel_y;
            label_sel_area.x2 = label->coords.x2;
            label_sel_area.y2 = label_sel_area.y1 + res_p.y;

            label_dsc.flag |= LV_TEXT_FLAG_EXPAND;
            lv_draw_label(&label_sel_area, &mask_sel, &label_dsc, lv_label_get_text(label), NULL);
        }

        lv_obj_draw_base(MY_CLASS, obj, clip_area, mode);
    }

    return LV_DRAW_RES_OK;
}

static lv_draw_res_t lv_roller_label_draw(lv_obj_t * label_obj, const lv_area_t * clip_area, lv_draw_mode_t mode)
{
    if(mode == LV_DRAW_MODE_COVER_CHECK) {
        return lv_obj_draw_base(MY_CLASS_LABEL, label_obj, clip_area, mode);
    }
    /*Draw the object*/
    else if(mode == LV_DRAW_MODE_MAIN_DRAW) {
        /* Split the drawing of the label into  an upper (above the selected area)
         * and a lower (below the selected area)*/
        lv_obj_t * roller = lv_obj_get_parent(label_obj);
        const lv_font_t * font = lv_obj_get_style_text_font(roller, LV_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(roller, LV_PART_MAIN);
        lv_coord_t font_h        = lv_font_get_line_height(font);

        lv_area_t rect_area;
        rect_area.y1 = roller->coords.y1 + (lv_obj_get_height(roller) - font_h - line_space) / 2;
        if((font_h & 0x1) && (line_space & 0x1)) rect_area.y1--; /*Compensate the two rounding error*/
        rect_area.y2 = rect_area.y1 + font_h + line_space - 1;
        lv_area_t roller_coords;
        lv_obj_get_coords(roller, &roller_coords);

        rect_area.x1 = roller_coords.x1;
        rect_area.x2 = roller_coords.x2;

        lv_area_t clip2;
        clip2.x1 = label_obj->coords.x1;
        clip2.y1 = label_obj->coords.y1;
        clip2.x2 = label_obj->coords.x2;
        clip2.y2 = rect_area.y1;
        if(_lv_area_intersect(&clip2, clip_area, &clip2)) {
            lv_obj_draw_base(MY_CLASS_LABEL, label_obj, clip_area, mode);
        }

        clip2.x1 = label_obj->coords.x1;
        clip2.y1 = rect_area.y2;
        clip2.x2 = label_obj->coords.x2;
        clip2.y2 = label_obj->coords.y2;
        if(_lv_area_intersect(&clip2, clip_area, &clip2)) {
            lv_obj_draw_base(MY_CLASS_LABEL, label_obj, clip_area, mode);
        }
    }

    return LV_DRAW_RES_OK;
}

static lv_res_t lv_roller_signal(lv_obj_t * obj, lv_signal_t sign, void * param)
{
    lv_res_t res;

    /* Include the ancient signal function */
    res = lv_obj_signal_base(MY_CLASS, obj, sign, param);
    if(res != LV_RES_OK) return res;

    lv_roller_t * roller = (lv_roller_t*)obj;

    if(sign == LV_SIGNAL_GET_SELF_SIZE) {
        lv_point_t * p = param;
        p->x =  get_selected_label_width(obj);
    }
    else if(sign == LV_SIGNAL_STYLE_CHG) {
        lv_obj_t * label = get_label(obj);
        /*Be sure the label's style is updated before processing the roller*/
        if(label) lv_signal_send(label, LV_SIGNAL_STYLE_CHG, NULL);
        lv_obj_handle_self_size_chg(obj);
        refr_position(obj, false);
    }
    else if(sign == LV_SIGNAL_COORD_CHG) {
        if(lv_obj_get_width(obj) != lv_area_get_width(param) ||
           lv_obj_get_height(obj) != lv_area_get_height(param))
        {
            refr_position(obj, false);
        }
    }
    else if(sign == LV_SIGNAL_PRESSED) {
        roller->moved = 0;
        lv_anim_del(get_label(obj), (lv_anim_exec_xcb_t)lv_obj_set_y);
    }
    else if(sign == LV_SIGNAL_PRESSING) {
        lv_indev_t * indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_vect(indev, &p);
        if(p.y) {
            lv_obj_t * label = get_label(obj);
            lv_obj_set_y(label, lv_obj_get_y(label) + p.y);
            roller->moved = 1;
        }
    }
    else if(sign == LV_SIGNAL_RELEASED) {
        release_handler(obj);
    }
    else if(sign == LV_SIGNAL_FOCUS) {
        lv_group_t * g             = lv_obj_get_group(obj);
        bool editing               = lv_group_get_editing(g);
        lv_indev_type_t indev_type = lv_indev_get_type(lv_indev_get_act());

        /*Encoders need special handling*/
        if(indev_type == LV_INDEV_TYPE_ENCODER) {
            /*In navigate mode revert the original value*/
            if(!editing) {
                if(roller->sel_opt_id != roller->sel_opt_id_ori) {
                    roller->sel_opt_id = roller->sel_opt_id_ori;
                    refr_position(obj, true);
                }
            }
            /*Save the current state when entered to edit mode*/
            else {
                roller->sel_opt_id_ori = roller->sel_opt_id;
            }
        }
        else {
            roller->sel_opt_id_ori = roller->sel_opt_id; /*Save the current value. Used to revert this state if
                                                                    ENTER won't be pressed*/
        }
    }
    else if(sign == LV_SIGNAL_DEFOCUS) {
        /*Revert the original state*/
        if(roller->sel_opt_id != roller->sel_opt_id_ori) {
            roller->sel_opt_id = roller->sel_opt_id_ori;
            refr_position(obj, true);
        }
    }
    else if(sign == LV_SIGNAL_CONTROL) {
        char c = *((char *)param);
        if(c == LV_KEY_RIGHT || c == LV_KEY_DOWN) {
            if(roller->sel_opt_id + 1 < roller->option_cnt) {
                uint16_t ori_id = roller->sel_opt_id_ori; /*lv_roller_set_selected will overwrite this*/
                lv_roller_set_selected(obj, roller->sel_opt_id + 1, true);
                roller->sel_opt_id_ori = ori_id;
            }
        }
        else if(c == LV_KEY_LEFT || c == LV_KEY_UP) {
            if(roller->sel_opt_id > 0) {
                uint16_t ori_id = roller->sel_opt_id_ori; /*lv_roller_set_selected will overwrite this*/

                lv_roller_set_selected(obj, roller->sel_opt_id - 1, true);
                roller->sel_opt_id_ori = ori_id;
            }
        }
    }

    return res;
}

/**
 * Signal function of the roller's label
 * @param label pointer to a roller's label object
 * @param sign a signal type from lv_signal_t enum
 * @param param pointer to a signal specific variable
 * @return LV_RES_OK: the object is not deleted in the function; LV_RES_INV: the object is deleted
 */
static lv_res_t lv_roller_label_signal(lv_obj_t * label, lv_signal_t sign, void * param)
{
    lv_res_t res;

    /* Include the ancient signal function */
    res = lv_obj_signal_base(MY_CLASS_LABEL, label, sign, param);
    if(res != LV_RES_OK) return res;

    if(sign == LV_SIGNAL_REFR_EXT_DRAW_SIZE) {
        /*If the selected text has a larger font it needs some extra space to draw it*/
        lv_coord_t * s = param;
        lv_obj_t * obj = lv_obj_get_parent(label);
        lv_coord_t sel_w = get_selected_label_width(obj);
        lv_coord_t label_w = lv_obj_get_width(label);
        *s = LV_MAX(*s, sel_w - label_w);
    }

    return res;
}

/**
 * Refresh the position of the roller. It uses the id stored in: roller->ddlist.selected_option_id
 * @param roller pointer to a roller object
 * @param anim_en LV_ANIM_ON: refresh with animation; LV_ANOM_OFF: without animation
 */
static void refr_position(lv_obj_t * obj, lv_anim_enable_t anim_en)
{
    lv_obj_t * label = get_label(obj);
    if(label == NULL) return;

    lv_text_align_t align = lv_obj_get_style_text_align(label, LV_PART_MAIN);
    switch(align) {
    case LV_TEXT_ALIGN_CENTER:
        lv_obj_set_x(label, (lv_obj_get_width_fit(obj) - lv_obj_get_width(label)) / 2);
        break;
    case LV_TEXT_ALIGN_RIGHT:
        lv_obj_set_x(label, lv_obj_get_width_fit(obj) - lv_obj_get_width(label));
        break;
    case LV_TEXT_ALIGN_LEFT:
        lv_obj_set_x(label, 0);
        break;
    }


    lv_roller_t * roller = (lv_roller_t*)obj;
    const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
    lv_coord_t font_h              = lv_font_get_line_height(font);
    lv_coord_t h                   = lv_obj_get_height_fit(obj);
    uint16_t anim_time             = lv_obj_get_style_anim_time(obj, LV_PART_MAIN);

    /* Normally the animation's `end_cb` sets correct position of the roller if infinite.
     * But without animations do it manually*/
    if(anim_en == LV_ANIM_OFF || anim_time == 0) {
        inf_normalize(obj);
    }


    int32_t id = roller->sel_opt_id;
    lv_coord_t sel_y1 = id * (font_h + line_space);
    lv_coord_t mid_y1 = h / 2 - font_h / 2;

    lv_coord_t new_y = mid_y1 - sel_y1;

    if(anim_en == LV_ANIM_OFF || anim_time == 0) {
        lv_anim_del(label, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_obj_set_y(label, new_y);
    }
    else {
        lv_anim_path_t path;
        lv_anim_path_init(&path);
        lv_anim_path_set_cb(&path, lv_anim_path_ease_out);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, label);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&a, lv_obj_get_y(label), new_y);
        lv_anim_set_time(&a, anim_time);
        lv_anim_set_ready_cb(&a, scroll_anim_ready_cb);
        lv_anim_set_path(&a, &path);
        lv_anim_start(&a);
    }
}

static lv_res_t release_handler(lv_obj_t * obj)
{

    lv_obj_t * label = get_label(obj);
    if(label == NULL) return LV_RES_OK;

    lv_indev_t * indev = lv_indev_get_act();
    lv_roller_t * roller = (lv_roller_t*)obj;

    /*Leave edit mode once a new option is selected*/
    lv_indev_type_t indev_type = lv_indev_get_type(indev);
    if(indev_type == LV_INDEV_TYPE_ENCODER || indev_type == LV_INDEV_TYPE_KEYPAD) {
        roller->sel_opt_id_ori = roller->sel_opt_id;

        if(indev_type == LV_INDEV_TYPE_ENCODER) {
            lv_group_t * g      = lv_obj_get_group(obj);
            if(lv_group_get_editing(g)) {
                lv_group_set_editing(g, false);
            }
        }
    }

    if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER || lv_indev_get_type(indev) == LV_INDEV_TYPE_BUTTON) {
        /*Search the clicked option (For KEYPAD and ENCODER the new value should be already set)*/
        int16_t new_opt  = -1;
        if(roller->moved == 0) {
            new_opt = 0;
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            p.y -= label->coords.y1;
            p.x -= label->coords.x1;
            uint32_t letter_i;
            letter_i = lv_label_get_letter_on(label, &p);

            const char * txt  = lv_label_get_text(label);
            uint32_t i        = 0;
            uint32_t i_prev   = 0;

            uint32_t letter_cnt = 0;
            for(letter_cnt = 0; letter_cnt < letter_i; letter_cnt++) {
                uint32_t letter = _lv_txt_encoded_next(txt, &i);
                /*Count he lines to reach the clicked letter. But ignore the last '\n' because it
                 * still belongs to the clicked line*/
                if(letter == '\n' && i_prev != letter_i) new_opt++;
                i_prev = i;
            }
        } else {
            /*If dragged then align the list to have an element in the middle*/
            const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
            lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
            lv_coord_t font_h              = lv_font_get_line_height(font);

            lv_coord_t label_unit = font_h + line_space;
            lv_coord_t mid        = obj->coords.y1 + (obj->coords.y2 - obj->coords.y1) / 2;
            lv_coord_t label_y1 = label->coords.y1 + lv_indev_scroll_throw_predict(indev, LV_DIR_VER);
            int32_t id = (mid - label_y1) / label_unit;

            if(id < 0) id = 0;
            if(id >= roller->option_cnt) id = roller->option_cnt - 1;

            new_opt = id;
        }

        if(new_opt >= 0) {
            lv_roller_set_selected(obj, new_opt, LV_ANIM_ON);
        }
    }

    uint32_t id  = roller->sel_opt_id; /*Just to use uint32_t in event data*/
    lv_res_t res = lv_event_send(obj, LV_EVENT_VALUE_CHANGED, &id);
    return res;
}

/**
 * Set the middle page for the roller if infinite is enabled
 * @param roller pointer to a roller object
 */
static void inf_normalize(lv_obj_t * obj)
{
    lv_roller_t * roller = (lv_roller_t*)obj;

    if(roller->mode == LV_ROLLER_MODE_INFINITE) {
        uint16_t real_id_cnt = roller->option_cnt / LV_ROLLER_INF_PAGES;
        roller->sel_opt_id = roller->sel_opt_id % real_id_cnt;
        roller->sel_opt_id += (LV_ROLLER_INF_PAGES / 2) * real_id_cnt; /*Select the middle page*/

        roller->sel_opt_id_ori = roller->sel_opt_id % real_id_cnt;
        roller->sel_opt_id_ori += (LV_ROLLER_INF_PAGES / 2) * real_id_cnt; /*Select the middle page*/

        /*Move to the new id*/
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
        lv_coord_t font_h              = lv_font_get_line_height(font);
        lv_coord_t h                   = lv_obj_get_height_fit(obj);

        lv_obj_t * label = get_label(obj);


        lv_coord_t sel_y1 = roller->sel_opt_id * (font_h + line_space);
        lv_coord_t mid_y1 = h / 2 - font_h / 2;
        lv_coord_t new_y = mid_y1 - sel_y1;
        lv_obj_set_y(label, new_y);
    }
}

static lv_obj_t * get_label(const lv_obj_t * obj)
{
    return lv_obj_get_child(obj, 0);
}


static lv_coord_t get_selected_label_width(const lv_obj_t * obj)
{
    lv_obj_t * label = get_label(obj);
    if(label == NULL) return 0;

    const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_SELECTED);
    lv_coord_t letter_space = lv_obj_get_style_text_letter_space(obj, LV_PART_SELECTED);
    const char * txt = lv_label_get_text(label);
    lv_point_t size;
    lv_txt_get_size(&size, txt, font, letter_space, 0, LV_COORD_MAX,  LV_TEXT_FLAG_NONE);
    return size.x;
}

static void scroll_anim_ready_cb(lv_anim_t * a)
{
    lv_obj_t * obj = lv_obj_get_parent(a->var); /*The label is animated*/
    inf_normalize(obj);
}
#endif
