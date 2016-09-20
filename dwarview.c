#include "dwarview.h"

/* DWARF tags.  */
struct tag_name {
	int tag;
	char *name;
};

#define DWARF_TAG(_t)  { DW_TAG_##_t, #_t }

static const struct tag_name tag_names[] = {
	DWARF_TAG(array_type),
	DWARF_TAG(class_type),
	DWARF_TAG(entry_point),
	DWARF_TAG(enumeration_type),
	DWARF_TAG(formal_parameter),
	DWARF_TAG(imported_declaration),
	DWARF_TAG(label),
	DWARF_TAG(lexical_block),
	DWARF_TAG(member),
	DWARF_TAG(pointer_type),
	DWARF_TAG(reference_type),
	DWARF_TAG(compile_unit),
	DWARF_TAG(string_type),
	DWARF_TAG(structure_type),
	DWARF_TAG(subroutine_type),
	DWARF_TAG(typedef),
	DWARF_TAG(union_type),
	DWARF_TAG(unspecified_parameters),
	DWARF_TAG(variant),
	DWARF_TAG(common_block),
	DWARF_TAG(common_inclusion),
	DWARF_TAG(inheritance),
	DWARF_TAG(inlined_subroutine),
	DWARF_TAG(module),
	DWARF_TAG(ptr_to_member_type),
	DWARF_TAG(set_type),
	DWARF_TAG(subrange_type),
	DWARF_TAG(with_stmt),
	DWARF_TAG(access_declaration),
	DWARF_TAG(base_type),
	DWARF_TAG(catch_block),
	DWARF_TAG(const_type),
	DWARF_TAG(constant),
	DWARF_TAG(enumerator),
	DWARF_TAG(file_type),
	DWARF_TAG(friend),
	DWARF_TAG(namelist),
	DWARF_TAG(namelist_item),
	DWARF_TAG(packed_type),
	DWARF_TAG(subprogram),
	DWARF_TAG(template_type_parameter),
	DWARF_TAG(template_value_parameter),
	DWARF_TAG(thrown_type),
	DWARF_TAG(try_block),
	DWARF_TAG(variant_part),
	DWARF_TAG(variable),
	DWARF_TAG(volatile_type),
	DWARF_TAG(dwarf_procedure),
	DWARF_TAG(restrict_type),
	DWARF_TAG(interface_type),
	DWARF_TAG(namespace),
	DWARF_TAG(imported_module),
	DWARF_TAG(unspecified_type),
	DWARF_TAG(partial_unit),
	DWARF_TAG(imported_unit),
	/* 0x3e reserved.  */
	DWARF_TAG(condition),
	DWARF_TAG(shared_type),
	DWARF_TAG(type_unit),
	DWARF_TAG(rvalue_reference_type),
	DWARF_TAG(template_alias),

	/* DWARF 5.  */
	DWARF_TAG(atomic_type),

	DWARF_TAG(lo_user),

	DWARF_TAG(MIPS_loop),
	DWARF_TAG(format_label),
	DWARF_TAG(function_template),
	DWARF_TAG(class_template),

	DWARF_TAG(GNU_BINCL),
	DWARF_TAG(GNU_EINCL),

	DWARF_TAG(GNU_template_template_param),
	DWARF_TAG(GNU_template_parameter_pack),
	DWARF_TAG(GNU_formal_parameter_pack),
	DWARF_TAG(GNU_call_site),
	DWARF_TAG(GNU_call_site_parameter),

	DWARF_TAG(hi_user),
};

#undef DWARF_TAG

char *dwarview_tag_name(int tag)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(tag_names); i++)
		if (tag_names[i].tag == tag)
			return tag_names[i].name;

	return "unknown";
}


/* DWARF attributes encodings.  */
struct attr_name {
	unsigned code;
	char *name;
};

#define DWARF_ATTR(_a)  { DW_AT_##_a, #_a }

static const struct attr_name attr_names[] = {
	DWARF_ATTR(sibling),
	DWARF_ATTR(location),
	DWARF_ATTR(name),
	DWARF_ATTR(ordering),
	DWARF_ATTR(subscr_data),
	DWARF_ATTR(byte_size),
	DWARF_ATTR(bit_offset),
	DWARF_ATTR(bit_size),
	DWARF_ATTR(element_list),
	DWARF_ATTR(stmt_list),
	DWARF_ATTR(low_pc),
	DWARF_ATTR(high_pc),
	DWARF_ATTR(language),
	DWARF_ATTR(member),
	DWARF_ATTR(discr),
	DWARF_ATTR(discr_value),
	DWARF_ATTR(visibility),
	DWARF_ATTR(import),
	DWARF_ATTR(string_length),
	DWARF_ATTR(common_reference),
	DWARF_ATTR(comp_dir),
	DWARF_ATTR(const_value),
	DWARF_ATTR(containing_type),
	DWARF_ATTR(default_value),
	DWARF_ATTR(inline),
	DWARF_ATTR(is_optional),
	DWARF_ATTR(lower_bound),
	DWARF_ATTR(producer),
	DWARF_ATTR(prototyped),
	DWARF_ATTR(return_addr),
	DWARF_ATTR(start_scope),
	DWARF_ATTR(bit_stride),
	DWARF_ATTR(upper_bound),
	DWARF_ATTR(abstract_origin),
	DWARF_ATTR(accessibility),
	DWARF_ATTR(address_class),
	DWARF_ATTR(artificial),
	DWARF_ATTR(base_types),
	DWARF_ATTR(calling_convention),
	DWARF_ATTR(count),
	DWARF_ATTR(data_member_location),
	DWARF_ATTR(decl_column),
	DWARF_ATTR(decl_file),
	DWARF_ATTR(decl_line),
	DWARF_ATTR(declaration),
	DWARF_ATTR(discr_list),
	DWARF_ATTR(encoding),
	DWARF_ATTR(external),
	DWARF_ATTR(frame_base),
	DWARF_ATTR(friend),
	DWARF_ATTR(identifier_case),
	DWARF_ATTR(macro_info),
	DWARF_ATTR(namelist_item),
	DWARF_ATTR(priority),
	DWARF_ATTR(segment),
	DWARF_ATTR(specification),
	DWARF_ATTR(static_link),
	DWARF_ATTR(type),
	DWARF_ATTR(use_location),
	DWARF_ATTR(variable_parameter),
	DWARF_ATTR(virtuality),
	DWARF_ATTR(vtable_elem_location),
	DWARF_ATTR(allocated),
	DWARF_ATTR(associated),
	DWARF_ATTR(data_location),
	DWARF_ATTR(byte_stride),
	DWARF_ATTR(entry_pc),
	DWARF_ATTR(use_UTF8),
	DWARF_ATTR(extension),
	DWARF_ATTR(ranges),
	DWARF_ATTR(trampoline),
	DWARF_ATTR(call_column),
	DWARF_ATTR(call_file),
	DWARF_ATTR(call_line),
	DWARF_ATTR(description),
	DWARF_ATTR(binary_scale),
	DWARF_ATTR(decimal_scale),
	DWARF_ATTR(small),
	DWARF_ATTR(decimal_sign),
	DWARF_ATTR(digit_count),
	DWARF_ATTR(picture_string),
	DWARF_ATTR(mutable),
	DWARF_ATTR(threads_scaled),
	DWARF_ATTR(explicit),
	DWARF_ATTR(object_pointer),
	DWARF_ATTR(endianity),
	DWARF_ATTR(elemental),
	DWARF_ATTR(pure),
	DWARF_ATTR(recursive),
	DWARF_ATTR(signature),
	DWARF_ATTR(main_subprogram),
	DWARF_ATTR(data_bit_offset),
	DWARF_ATTR(const_expr),
	DWARF_ATTR(enum_class),
	DWARF_ATTR(linkage_name),

	/* DWARF5 attribute values.  */
	DWARF_ATTR(noreturn),

	DWARF_ATTR(lo_user),

	DWARF_ATTR(MIPS_fde),
	DWARF_ATTR(MIPS_loop_begin),
	DWARF_ATTR(MIPS_tail_loop_begin),
	DWARF_ATTR(MIPS_epilog_begin),
	DWARF_ATTR(MIPS_loop_unroll_factor),
	DWARF_ATTR(MIPS_software_pipeline_depth),
	DWARF_ATTR(MIPS_linkage_name),
	DWARF_ATTR(MIPS_stride),
	DWARF_ATTR(MIPS_abstract_name),
	DWARF_ATTR(MIPS_clone_origin),
	DWARF_ATTR(MIPS_has_inlines),
	DWARF_ATTR(MIPS_stride_byte),
	DWARF_ATTR(MIPS_stride_elem),
	DWARF_ATTR(MIPS_ptr_dopetype),
	DWARF_ATTR(MIPS_allocatable_dopetype),
	DWARF_ATTR(MIPS_assumed_shape_dopetype),
	DWARF_ATTR(MIPS_assumed_size),

	/* GNU extensions.  */
	DWARF_ATTR(sf_names),
	DWARF_ATTR(src_info),
	DWARF_ATTR(mac_info),
	DWARF_ATTR(src_coords),
	DWARF_ATTR(body_begin),
	DWARF_ATTR(body_end),
	DWARF_ATTR(GNU_vector),
	DWARF_ATTR(GNU_guarded_by),
	DWARF_ATTR(GNU_pt_guarded_by),
	DWARF_ATTR(GNU_guarded),
	DWARF_ATTR(GNU_pt_guarded),
	DWARF_ATTR(GNU_locks_excluded),
	DWARF_ATTR(GNU_exclusive_locks_required),
	DWARF_ATTR(GNU_shared_locks_required),
	DWARF_ATTR(GNU_odr_signature),
	DWARF_ATTR(GNU_template_name),
	DWARF_ATTR(GNU_call_site_value),
	DWARF_ATTR(GNU_call_site_data_value),
	DWARF_ATTR(GNU_call_site_target),
	DWARF_ATTR(GNU_call_site_target_clobbered),
	DWARF_ATTR(GNU_tail_call),
	DWARF_ATTR(GNU_all_tail_call_sites),
	DWARF_ATTR(GNU_all_call_sites),
	DWARF_ATTR(GNU_all_source_call_sites),
	DWARF_ATTR(GNU_macros),
	DWARF_ATTR(GNU_deleted),

	DWARF_ATTR(hi_user),
};

#undef DWARF_ATTR

char *dwarview_attr_name(unsigned int attr)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(attr_names); i++)
		if (attr_names[i].code == attr)
			return attr_names[i].name;

	return "unknown";
}

/* DWARF form encodings.  */
struct form_name {
	unsigned form;
	char *name;
};

#define DWARF_FORM(_f)  { DW_FORM_##_f, #_f }

static const struct form_name form_names[] = {
	DWARF_FORM(addr),
	DWARF_FORM(block2),
	DWARF_FORM(block4),
	DWARF_FORM(data2),
	DWARF_FORM(data4),
	DWARF_FORM(data8),
	DWARF_FORM(string),
	DWARF_FORM(block),
	DWARF_FORM(block1),
	DWARF_FORM(data1),
	DWARF_FORM(flag),
	DWARF_FORM(sdata),
	DWARF_FORM(strp),
	DWARF_FORM(udata),
	DWARF_FORM(ref_addr),
	DWARF_FORM(ref1),
	DWARF_FORM(ref2),
	DWARF_FORM(ref4),
	DWARF_FORM(ref8),
	DWARF_FORM(ref_udata),
	DWARF_FORM(indirect),
	DWARF_FORM(sec_offset),
	DWARF_FORM(exprloc),
	DWARF_FORM(flag_present),
	DWARF_FORM(ref_sig8),

	DWARF_FORM(GNU_ref_alt),
	DWARF_FORM(GNU_strp_alt),
};

char *dwarview_form_name(unsigned int form)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(form_names); i++)
		if (form_names[i].form == form)
			return form_names[i].name;

	return "unknown";
}

#undef DWARF_FORM
