#include "GCCObjectFormatParser.h"
#include "Common.h"
#include "entry.hpp"
#include "Debug.h"
#include "demangle.hpp"
#include "name.hpp"
#include "offset.hpp"
#include "nalt.hpp"
#include "bytes.hpp"
#include "Utility.h"
#include "stddef.h"
#include "GCCVtableInfo.h"
#include "GCCTypeInfo.h"
#include "struct.hpp"
#include "Debug.h"
#include "ReconstructableType.h"
#include <stack>

#define vmi_class_type_info_name "_ZTVN10__cxxabiv121__vmi_class_type_infoE"
#define class_type_info_name "_ZTVN10__cxxabiv117__class_type_infoE"
#define si_class_type_info_name "_ZTVN10__cxxabiv120__si_class_type_infoE"

std::map<ea_t, GCCVtableInfo *>g_KnownVtables;
std::map<ea_t, GCCTypeInfo *>g_KnownTypes;
std::map<std::string, GCCVtableInfo *>g_KnownVtableNames;
std::map<std::string, GCCTypeInfo *>g_KnownTypeNames;

ea_t class_type_info_vtbl = -1;
ea_t si_class_type_info_vtbl = -1;
ea_t vmi_class_type_info_vtbl = -1;


std::set <GCCTypeInfo *> visitedTypes;
std::set <ReconstructableType *> visited;

static void buildReconstructableTypes();

GCCObjectFormatParser::GCCObjectFormatParser()
{
}

GCCObjectFormatParser::~GCCObjectFormatParser()
{
}


int  import_enum_cb(ea_t ea, const char *name, uval_t ord, void *param) {
	GCCObjectFormatParser *parser = (GCCObjectFormatParser*)param;
	if (name == 0)
		return 1;
	//msg("import name %s\n", name);
	ea += sizeof(GCC_RTTI::__vtable_info); // BUG From IDA. Hello funny imports.
	if (class_type_info_vtbl == BADADDR && !memcmp(class_type_info_name, name, sizeof(class_type_info_name) - 1))
	{
		class_type_info_vtbl = ea;
		set_name(class_type_info_vtbl, "__cxxabiv1::__class_type_info::vtable", SN_NOWARN);
	}

	if (si_class_type_info_vtbl == BADADDR && !memcmp(si_class_type_info_name, name, sizeof(si_class_type_info_name) - 1))
	{
		si_class_type_info_vtbl = ea;
		set_name(si_class_type_info_vtbl, "__cxxabiv1::__si_class_type_info::vtable", SN_NOWARN);
	}

	if (vmi_class_type_info_vtbl == BADADDR && !memcmp(vmi_class_type_info_name, name, sizeof(vmi_class_type_info_name) - 1))
	{
		vmi_class_type_info_vtbl = ea;
		set_name(vmi_class_type_info_vtbl, "__cxxabiv1::__vmi_class_type_info::vtable", SN_NOWARN);
	}

	return 1;
}

int __get_ea_of_name(size_t index, ea_t *value) {
	ea_t ea = get_nlist_ea(index);
	if (ea == BADADDR)
		return -1;
	*value = ea;
	return 0;
}

int find_vtbls_by_name() {
	size_t cnt = get_nlist_size();
	unsigned int found_vtbls = 0;
	ea_t ea;
	if (class_type_info_vtbl != BADADDR)
		++found_vtbls;
	if (si_class_type_info_vtbl != BADADDR)
		++found_vtbls;
	if (vmi_class_type_info_vtbl != BADADDR)
		++found_vtbls;
	for (size_t i = 0; i < cnt && found_vtbls < 3; ++i) {
		const char *name = get_nlist_name(i);
		if (name && memcmp(name, "_ZTVN10__cxxabiv", sizeof("_ZTVN10__cxxabiv")-1) == 0) {
			if (class_type_info_vtbl == BADADDR)
				if (memcmp(name, class_type_info_name, sizeof(class_type_info_name)-1) == 0)
					if (!__get_ea_of_name(i, &ea))
					{
						ea += sizeof(GCC_RTTI::__vtable_info);
						class_type_info_vtbl = ea;
						++found_vtbls;
						continue;
					}
			if (si_class_type_info_vtbl == BADADDR)
				if (memcmp(name, si_class_type_info_name, sizeof(si_class_type_info_name)-1) == 0)
					if (!__get_ea_of_name(i, &ea))
					{
						ea += sizeof(GCC_RTTI::__vtable_info);
						si_class_type_info_vtbl = ea;
						++found_vtbls;
						continue;
					}
			if (vmi_class_type_info_vtbl == BADADDR)
				if (memcmp(name, vmi_class_type_info_name, sizeof(vmi_class_type_info_name)-1) == 0)
					if (!__get_ea_of_name(i, &ea))
					{
						ea += sizeof(GCC_RTTI::__vtable_info);
						vmi_class_type_info_vtbl = ea;
						++found_vtbls;
						continue;
					}
		}
	}
	return 0;
}

void GCCObjectFormatParser::getRttiInfo()
{
	qstring buffer;
	size_t count = get_entry_qty();
	
	// First collect info about __cxxabiv1:: vtables
	for (int i = 0; i < count; ++i) {
		uval_t ordinal = get_entry_ordinal(i);
		get_entry_name(&buffer, ordinal);
		ea_t ea = get_entry(ordinal);
		ea += sizeof(GCC_RTTI::__vtable_info);
			
		if (class_type_info_vtbl == BADADDR && !memcmp(class_type_info_name, buffer.c_str(), sizeof(class_type_info_name) - 1))
		{
			class_type_info_vtbl = ea;
			set_name(ea, "__cxxabiv1::__class_type_info::vtable", SN_NOWARN);
		}
		
		if (si_class_type_info_vtbl == BADADDR && !memcmp(si_class_type_info_name, buffer.c_str(), sizeof(si_class_type_info_name) - 1))
		{
			si_class_type_info_vtbl = ea;
			set_name(ea, "__cxxabiv1::__si_class_type_info::vtable", SN_NOWARN);
		}

		if (vmi_class_type_info_vtbl == BADADDR && !memcmp(vmi_class_type_info_name, buffer.c_str(), sizeof(vmi_class_type_info_name) - 1))
		{
			vmi_class_type_info_vtbl = ea;
			set_name(ea, "__cxxabiv1::__vmi_class_type_info::vtable", SN_NOWARN);
		}
		//msg("export %s\n", buffer.c_str());
	}

	count = get_import_module_qty();
	for (uint index = 0; index < count; ++index) {
		if (get_import_module_name(&buffer, index))
		{
			//msg("import module %s\n", buffer.c_str());
		}
		enum_import_names(index, &import_enum_cb, this);
	}
	find_vtbls_by_name();

	
	if (class_type_info_vtbl == -1 &&
		si_class_type_info_vtbl == -1 &&
		vmi_class_type_info_vtbl == -1)
		return;
		// if no any rtti vtables, we cant read it.
	// now we can scan  segments for vtables.
	int segCount = get_segm_qty();
	for (int i = 0; i < segCount; i++)
	{
		if (segment_t *seg = getnseg(i))
		{
			if (seg->type == SEG_DATA)
			{
				scanSeg4Vftables(seg);
			}
		}
	}

	buildReconstructableTypes();
}

void GCCObjectFormatParser::scanSeg4Vftables(segment_t *seg)
{
	UINT found = 0;
	unsigned int counter = 0;
	if (seg->size() >= sizeof(ea_t))
	{
		ea_t startEA = ((seg->start_ea + sizeof(ea_t)) & ~((ea_t)(sizeof(ea_t) - 1)));
		ea_t endEA = (seg->end_ea - sizeof(ea_t));
		
		for (ea_t ptr = startEA; ptr < endEA; ptr += sizeof(ea_t))
		{
			// Struct of vtable is following:
			// 0: ptrdiff that tells "Where is the original object according to vtable. This one is 0 of -x;
			// 1*sizeof(ea_t): ptr to type_info
			// 2*sizeof(ea_t) ... : the exact functions.
			// So if we can parse type_info as type_info and we see functions, it should be vtable.
			//ea_t ea = getEa(ptr);
			//flags_t flags = get_flags_novalue(ea);
			//if (isData(flags))
			//{
				GCCVtableInfo * info = GCCVtableInfo::parseVtableInfo(ptr);
				if (info)
				{
					VTBL_info_t vtbl_info;
					vtbl_info.ea_begin = info->ea_start + sizeof(GCC_RTTI::__vtable_info);
					vtbl_info.ea_end = info->ea_end;
					vtbl_info.vtbl_name = info->typeName.c_str();
					vtbl_info.methods = info->vtables[0].methodsCount;
					rtti_vftables[ptr + sizeof(GCC_RTTI::__vtable_info)] = vtbl_info;
					ptr = info->ea_end;
				}
				else {

					GCCTypeInfo *typeInfo = GCCTypeInfo::parseTypeInfo(ptr);
					if (typeInfo)
					{
						counter++;
						;
					}

				}

			//}
		}
	}

	return;
}

void GCCObjectFormatParser::clearInfo()
{
	g_KnownVtables.clear();
	g_KnownVtableNames.clear();
	g_KnownTypes.clear();
	g_KnownTypeNames.clear();
	assert(false); // reasonable question what to do with ReconstructableTypes.
}

void buildReconstructableTypesRecursive(GCCTypeInfo *type) {
	/*
	std::map<std::string, ReconstructableType*> reTypesCopy;
	std::map<std::string, ReconstructableType*>::iterator reconstructedTypesIt;
	for (reconstructedTypesIt = g_ReconstractedTypes.begin(); reconstructedTypesIt != g_ReconstractedTypes.end(); ++reconstructedTypesIt) {

		ReconstructableType* type = reconstructedTypesIt->second;
		if (reTypesCopy.count(type->name) != 0) // ignore what we already proceed
			continue;
		if (!ends_with(type->name, VTBL_CLSNAME_POSTFIX)) // ignore non-vtables
			continue;
		if (g_KnownVtableNames.count(type->name) == 0) {
			//assert(false);
			msg("Failed to find vtable for %s", type->name.c_str());
			continue;
		}
		GCCVtableInfo *info = g_KnownVtableNames[type->name];
		//assert(info);
		GCCTypeInfo *typeInfo = info->typeInfo;

		for (unsigned int i = 0; i < typeInfo->parentsCount; ++i) {
			GCCTypeInfo *parentInfo = typeInfo->parentsTypes[i]->info;
			assert(parentInfo);
			if (g_ReconstractedTypes.count(parentInfo->typeName + VTBL_CLSNAME_POSTFIX) == 0)
				continue; // for those who dont have vtables in base class.
			ReconstructableType* parentType = g_ReconstractedTypes[parentInfo->typeName + VTBL_CLSNAME_POSTFIX];
			ReconstructableMember *newMember = new ReconstructableMember();
			newMember->offset = typeInfo->parentsTypes[i]->offset;
			newMember->name = parentInfo->typeName;

			type->AddDerivedMember(newMember);
		}
		reTypesCopy[type->name] = type;
	}
	*/
	if (visitedTypes.count(type))
		return;
	// Handle parents first
	if (type->parentsCount)
	{
		for (unsigned long i = 0; i < type->parentsCount; ++i) {
			GCCParentType * parent = type->parentsTypes[i];
			buildReconstructableTypesRecursive(parent->info);
		}
	}

	ReconstructableType *reType;
	if (g_ReconstractedTypes.count(type->typeName)) {
		reType = g_ReconstractedTypes[type->typeName];
		assert(false); // probably we already visited this type, need to check it.
	}
	else {
		reType = ReconstructableType::getReconstructableType(type->typeName);
		reType->SyncTypeInfo();
	}
	// type->ea;
	// type->parentsCount;
	// type->typeinfo_vtbl;
	// type->vtable;
	if (type->vtable) // type has vtable;
	{	
		GCCVtableInfo *vtblInfo = type->vtable;
		std::string vtbl_class_name = type->typeName + VTBL_CLSNAME_POSTFIX;
		if (g_ReconstractedTypes.count(vtbl_class_name)) {
			assert(false); // one more assert for the future
		}
		char buffer[256];

		for (unsigned int i = 0; i < vtblInfo->vtablesCount; ++i) {
			unsigned int offset = (unsigned int)(-(signed int)vtblInfo->vtables[i].ptrDiff);
			std::string parentName = vtblInfo->vtables[i].name.c_str();
			std::string vtblName = vtbl_class_name;
			if (i != 0) {
				snprintf(buffer, sizeof(buffer), "%s_%x_of_%s", vtbl_class_name.c_str(), offset, parentName.c_str());
				vtblName = buffer;
			}
			ReconstructableType * reVtbl = ReconstructableTypeVtable::getReconstructableTypeVtable(vtblName, vtblInfo->ea_start);
			if (i != 0) {
				ReconstructableType *parent;
				if (g_ReconstractedTypes.count(type->parentsTypes[i]->info->typeName))
				{
					parent = g_ReconstractedTypes[type->parentsTypes[i]->info->typeName];
					std::map<unsigned int, ReconstructableMember *> pmembers = parent->getOwnMembers();
					if (parent->getSize() < sizeof(uval_t)) {
						ReconstructableType *parentVtbl = ReconstructableType::getReconstructableType(parent->name + VTBL_CLSNAME_POSTFIX);
						if (parentVtbl->getSize() < vtblInfo->vtables[i].methodsCount) {
							for (unsigned int methodIndx = parentVtbl->getSize(); methodIndx < vtblInfo->vtables[i].methodsCount; ++methodIndx)
							{
								ReconstructableMember *pmethod = new ReconstructableMember();
								pmethod->name = "purecall"; 
								pmethod->name += std::to_string(methodIndx);
								pmethod->offset = methodIndx * sizeof(uval_t);
								tinfo_t info = dummy_ptrtype(sizeof(uval_t), 0);
								pmethod->memberType = new MemberTypeIDATypeInfoGate(info);
								parentVtbl->AddMember(pmethod);
							}
						}
						ReconstructableMember *pmember = new ReconstructableMember();
						pmember->name = "vtable";
						pmember->offset = 0;
						pmember->memberType = new MemberTypePointer(parentVtbl->name);
						parent->AddMember(pmember);
					}
				}
			}
			

			for (unsigned int j = 0; j < vtblInfo->vtables[i].methodsCount; ++j) {
				ReconstructableMember *member = new ReconstructableMember();
				member->offset = sizeof(uval_t)*j;
				ea_t funcPtr = getEa(vtblInfo->vtables[i].ea + sizeof(uval_t)*j + sizeof(GCC_RTTI::__vtable_info));
				if (funcPtr == 0) {
					member->name = "purecall";
					member->name += std::to_string(j);
				}
					
				else {
					if (ph.id == PLFM_ARM)
						funcPtr &= (ea_t)-2;
					qstring method_name;
					get_ea_name(&method_name, funcPtr);
					if (method_name.find("sub_", 0) == 0 || method_name.length() == 0) {
						// we can rename it.
						qstring newName;
						newName.sprnt("%s::refunc_%x", type->typeName.c_str(), funcPtr);
						if (set_name(funcPtr, newName.c_str(), SN_NOWARN)) {
							method_name = newName;
						}	
					}
					if (method_name.length() == 0)
						method_name.sprnt("___refunc_%x", funcPtr);
					member->name = method_name.c_str();
				}

				tinfo_t info = dummy_ptrtype(sizeof(uval_t), 0);
				member->memberType = new  MemberTypeIDATypeInfoGate(info);
				reVtbl->AddMember(member);
			}
			if (i == 0) {
				// add vtable info
				ReconstructableMember *member = new ReconstructableMember();
				member->name = "vtable";
				member->offset = 0;
				member->memberType = new MemberTypePointer(vtbl_class_name);
				reType->AddMember(member);
			}
			if (i < type->parentsCount && g_ReconstractedTypes.count(type->parentsTypes[i]->info->typeName + VTBL_CLSNAME_POSTFIX)) {
				
				ReconstructableType *parentVtbl = g_ReconstractedTypes[type->parentsTypes[i]->info->typeName + VTBL_CLSNAME_POSTFIX];
				
				ReconstructableMember *dmember = new ReconstructableMember();
				dmember->name = parentVtbl->name;
				dmember->offset = 0;

				dmember->memberType = new ReconstructedMemberReType(parentVtbl);
				reVtbl->AddDerivedMember(dmember);
				


			}
			reVtbl->SyncTypeInfo();
			// we have vtable, we have it as structure, lets apply its name and type to IDB
			if (i == 0) {
				std::string idb_name = type->typeName + "::_vftable";
				ea_t ea = vtblInfo->vtables[0].ea + 2 * sizeof(uval_t);
				setUnknown(ea, vtblInfo->vtables[0].methodsCount * sizeof(uval_t));
				MakeName(ea, idb_name.c_str());
				tinfo_t tinfo;
				if (tinfo.get_named_type(get_idati(), reVtbl->name.c_str())) {
					apply_tinfo(ea, tinfo, TINFO_DEFINITE);
				}
			}
		}
	}
	for (unsigned int i = 0; i < type->parentsCount; ++i) {
		assert(g_ReconstractedTypes.count(type->parentsTypes[i]->info->typeName));
		ReconstructableType *parent = g_ReconstractedTypes[type->parentsTypes[i]->info->typeName];
		type->parentsTypes[i];
		ReconstructableMember* member = new ReconstructableMember();
		member->offset = type->parentsTypes[i]->offset;
		member->name = type->parentsTypes[i]->info->typeName;
		member->memberType = new ReconstructedMemberReType(parent);
		reType->AddDerivedMember(member);
	}
	visitedTypes.emplace(type);
}


void fixupRecounstructableTypesId() {
	unsigned long id = 0;
	for (auto iterator = g_ReconstractedTypes.begin(); iterator != g_ReconstractedTypes.end(); iterator++, id++)
	{
		iterator->second->id = id;
	}
}

static void buildReconstructableTypes() {
	visitedTypes.clear();
	SyncTypeInfoMethod curMethod = syncTypeInfoMethod;
	syncTypeInfoMethod = SyncTypeInfo_Names;
	std::map<ea_t, GCCTypeInfo *>::iterator typesIterator;
	for (typesIterator = g_KnownTypes.begin(); typesIterator != g_KnownTypes.end(); ++typesIterator) {
		GCCTypeInfo *curType = typesIterator->second;
		if (visitedTypes.count(curType))
			continue; // already parsed
		buildReconstructableTypesRecursive(curType);
	}
	fixupRecounstructableTypesId();
	syncTypeInfoMethod = curMethod;
	for (auto typeIt = g_ReconstractedTypes.begin(); typeIt != g_ReconstractedTypes.end(); ++typeIt) {
		typeIt->second->SyncTypeInfo();
	}

	return;
}

void reconstructVtablesRecursive(ReconstructableType *type) {
	if (visited.count(type))
		return;
	if (!ends_with(type->name, VTBL_CLSNAME_POSTFIX))
	{
		visited.emplace(type);
		return;
	}
	if (type->getParents().size())
	{
		std::set<ReconstructableType*> parents = type->getParents();
		std::set<ReconstructableType*>::iterator it;
		for (it = parents.begin(); it != parents.end(); ++it)
			reconstructVtablesRecursive(*it);
	}
	if (!g_KnownVtableNames.count(type->name))
	{
		msg("Failed to find vtable %s\n", type->name);
		visited.emplace(type);
		return;
	}
	GCCVtableInfo *vtblInfo = g_KnownVtableNames[type->name];

	vtblInfo->vtables[0].methodsCount;

}

void reconstructVtables() {
	visited.clear();
	std::map<std::string, ReconstructableType*>::iterator it;
	for (it = g_ReconstractedTypes.begin(); it != g_ReconstractedTypes.end(); ++it)
		reconstructVtablesRecursive(it->second);
}