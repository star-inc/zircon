// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTICS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTICS_H_

#include "diagnostic_types.h"

namespace fidl {
namespace diagnostics {

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
constexpr ErrorDef<std::string_view> ErrInvalidCharacter("invalid character '{}'");

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
constexpr ErrorDef ErrExpectedDeclaration("expected start of declaration");
constexpr ErrorDef ErrUnexpectedToken("found unexpected token");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedTokenOfKind(
    "unexpected token {}, was expecting {}");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedIdentifier(
    "unexpected identifier {}, was expecting {}");
constexpr ErrorDef<std::string> ErrInvalidIdentifier("invalid identifier '{}'");
constexpr ErrorDef<std::string> ErrInvalidLibraryNameComponent("Invalid library name component {}");
constexpr ErrorDef<std::string> ErrDuplicateAttribute("duplicate attribute with name '{}'");
constexpr ErrorDef ErrMissingOrdinalBeforeType("missing ordinal before type");
constexpr ErrorDef ErrOrdinalOutOfBound("ordinal out-of-bound");
constexpr ErrorDef ErrOrdinalsMustStartAtOne("ordinals must start at 1");
constexpr ErrorDef ErrCompoundAliasIdentifier("alias identifiers cannot contain '.'");
constexpr ErrorDef ErrMustHaveOneMember("must have at least one member");
constexpr ErrorDef ErrCannotAttachAttributesToCompose("Cannot attach attributes to compose stanza");
constexpr ErrorDef ErrUnrecognizedProtocolMember("unrecognized protocol member");
constexpr ErrorDef ErrExpectedProtocolMember("expected protocol member");
constexpr ErrorDef ErrCannotAttachAttributesToReservedOrdinals(
    "Cannot attach attributes to reserved ordinals");
constexpr ErrorDef<Token::KindAndSubkind> ErrExpectedOrdinalOrCloseBrace(
    "Expected one of ordinal or '}', found {}");
constexpr ErrorDef ErrMustHaveNonReservedMember(
    "must have at least one non reserved member; you can use an empty struct to "
    "define a placeholder variant");
constexpr ErrorDef<Token::KindAndSubkind> ErrCannotSpecifyFlexible(
    "cannot specify flexible for {}");
constexpr ErrorDef<Token::KindAndSubkind> ErrCannotSpecifyStrict(
    "cannot specify strictness for {}");
constexpr ErrorDef ErrDocCommentOnParameters("cannot have doc comment on parameters");
constexpr ErrorDef ErrXunionDeprecated("xunion is deprecated, please use `flexible union` instead");
constexpr ErrorDef ErrStrictXunionDeprecated(
    "strict xunion is deprecated, please use `strict union` instead");
constexpr WarningDef WarnCommentWithinDocCommentBlock(
    "cannot have comment within doc comment block");
constexpr WarningDef WarnBlankLinesWithinDocCommentBlock(
    "cannot have blank lines within doc comment block");
constexpr WarningDef WarnDocCommentMustBeFollowedByDeclaration(
    "doc comment must be followed by a declaration");
constexpr WarningDef WarnLibraryImportsMustBeGroupedAtTopOfFile(
    "library imports must be grouped at top-of-file");

// ---------------------------------------------------------------------------
// Library::ConsumeFile: Consume* methods and declaration registration
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name> ErrNameCollision("Name collision: {}");
constexpr ErrorDef<flat::Name> ErrDeclNameConflictsWithLibraryImport(
    "Declaration name '{}' conflicts with a library import; consider using the "
    "'as' keyword to import the library under a different name.");
constexpr ErrorDef ErrFilesDisagreeOnLibraryName(
    "Two files in the library disagree about the name of the library");
constexpr ErrorDef<std::vector<std::string_view>> ErrDuplicateLibraryImport(
    "Library {} already imported. Did you require it twice?");
constexpr ErrorDef<raw::AttributeList> ErrAttributesNotAllowedOnLibraryImport(
    "no attributes allowed on library import, found: {}");
constexpr ErrorDef<std::vector<std::string_view>> ErrUnknownLibrary(
    "Could not find library named {}. Did you include its sources with --files?");
constexpr ErrorDef ErrProtocolComposedMultipleTimes("protocol composed multiple times");
constexpr ErrorDef ErrDefaultsOnTablesNotSupported("Defaults on table members are not supported.");
constexpr ErrorDef ErrDefaultsOnUnionsNotSupported("Defaults on union members are not supported.");
constexpr ErrorDef ErrNullableTableMember("Table members cannot be nullable");
constexpr ErrorDef ErrNullableUnionMember("Union members cannot be nullable");

// ---------------------------------------------------------------------------
// Library::Compile: SortDeclarations
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name> ErrFailedConstantLookup("Unable to find the constant named: {}");
constexpr ErrorDef ErrIncludeCycle("There is an includes-cycle in declarations");

// ---------------------------------------------------------------------------
// Library::Compile: Compilation, Resolution, Validation
// ---------------------------------------------------------------------------
constexpr ErrorDef<std::vector<std::string_view>, std::vector<std::string_view>>
    ErrUnknownDependentLibrary(
        "Unknown dependent library {} or reference to member of "
        "library {}. Did you require it with `using`?");
constexpr ErrorDef<const flat::Type *> ErrInvalidConstantType("invalid constant type {}");
constexpr ErrorDef ErrCannotResolveConstantValue("unable to resolve constant value");
constexpr ErrorDef ErrOrOperatorOnNonPrimitiveValue(
    "Or operator can only be applied to primitive-kinded values");
constexpr ErrorDef ErrUnknownEnumMember("Unknown enum member");
constexpr ErrorDef ErrUnknownBitsMember("Unknown bits member");
constexpr ErrorDef<flat::IdentifierConstant *> ErrExpectedValueButGotType(
    "{} is a type, but a value was expected");
constexpr ErrorDef<flat::Name, flat::Name> ErrMismatchedNameTypeAssignment(
    "mismatched named type assignment: cannot define a constant or default value of type {} "
    "using a value of type {}");
constexpr ErrorDef<flat::IdentifierConstant *, const flat::TypeConstructor *, const flat::Type *>
    ErrCannotConvertConstantToType("{}, of type {}, cannot be converted to type {}");
constexpr ErrorDef<flat::LiteralConstant *, uint64_t, const flat::Type *>
    ErrStringConstantExceedsSizeBound("{} (string:{}) exceeds the size bound of type {}");
constexpr ErrorDef<flat::LiteralConstant *, const flat::Type *>
    ErrConstantCannotBeInterpretedAsType("{} cannot be interpreted as type {}");
constexpr ErrorDef ErrCouldNotResolveIdentifierToType("could not resolve identifier to a type");
constexpr ErrorDef ErrBitsMemberMustBePowerOfTwo("bits members must be powers of two");
constexpr ErrorDef<std::string, std::string, std::string, std::string>
    ErrFlexibleEnumMemberWithMaxValue(
        "flexible enums must not have a member with a value of {}, which is "
        "reserved for the unknown value. either: remove the member with the {} "
        "value, change the member with the {} value to something other than {}, or "
        "explicitly specify the unknown value with the [Unknown] attribute. see "
        "<https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/"
        "language#unions> for more info.");
constexpr ErrorDef<const flat::Type *> ErrBitsTypeMustBeUnsignedIntegralPrimitive(
    "bits may only be of unsigned integral primitive type, found {}");
constexpr ErrorDef<const flat::Type *> ErrEnumTypeMustBeIntegralPrimitive(
    "enums may only be of integral primitive type, found {}");
constexpr ErrorDef ErrUnknownAttributeOnInvalidType(
    "[Unknown] attribute can be only be used on flexible or [Transitional] types.");
constexpr ErrorDef ErrUnknownAttributeOnMultipleMembers(
    "[Unknown] attribute can be only applied to one member.");
constexpr ErrorDef ErrComposingNonProtocol("This declaration is not a protocol");
constexpr ErrorDef<SourceSpan> ErrDuplicateMethodName(
    "Multiple methods with the same name in a protocol; last occurrence was at {}");
constexpr ErrorDef ErrGeneratedZeroValueOrdinal("Ordinal value 0 disallowed.");
constexpr ErrorDef<SourceSpan, std::string> ErrDuplicateMethodOrdinal(
    "Multiple methods with the same ordinal in a protocol; previous was at {}. "
    "Consider using attribute [Selector=\"{}\"] to change the name used to "
    "calculate the ordinal.");
constexpr ErrorDef<SourceSpan> ErrDuplicateMethodParameterName(
    "Multiple parameters with the same name in a method; previous was at {}");
constexpr ErrorDef<SourceSpan> ErrDuplicateServiceMemberName(
    "multiple service members with the same name; previous was at {}");
constexpr ErrorDef ErrNonProtocolServiceMember("only protocol members are allowed");
constexpr ErrorDef ErrNullableServiceMember("service members cannot be nullable");
constexpr ErrorDef<SourceSpan> ErrDuplicateStructMemberName(
    "Multiple struct fields with the same name; previous was at {}");
constexpr ErrorDef<std::string, const flat::Type *> ErrInvalidStructMemberType(
    "struct field {} has an invalid default type{}");
constexpr ErrorDef<SourceSpan> ErrDuplicateTableFieldOrdinal(
    "Multiple table fields with the same ordinal; previous was at {}");
constexpr ErrorDef<SourceSpan> ErrDuplicateTableFieldName(
    "Multiple table fields with the same name; previous was at {}");
constexpr ErrorDef<SourceSpan> ErrDuplicateUnionMemberOrdinal(
    "Multiple union fields with the same ordinal; previous was at {}");
constexpr ErrorDef<SourceSpan> ErrDuplicateUnionMemberName(
    "Multiple union members with the same name; previous was at {}");
constexpr ErrorDef<uint32_t> ErrNonDenseOrdinal(
    "missing ordinal {} (ordinals must be dense); consider marking it reserved");
constexpr ErrorDef ErrCouldNotResolveHandleRights("unable to resolve handle rights");
constexpr ErrorDef ErrCouldNotParseSizeBound("unable to parse size bound");
constexpr ErrorDef<std::string> ErrCouldNotResolveMember("unable to resolve {} member");
constexpr ErrorDef<std::string, std::string, flat::Name> ErrDuplicateMemberName(
    "name of member {} conflicts with previously declared member in the {} {}");
constexpr ErrorDef<std::string, std::string, std::string, flat::Name> ErrDuplicateMemberValue(
    "value of member {} conflicts with previously declared member {} in the {} {}");

// ---------------------------------------------------------------------------
// Attribute Validation: Placement, Values, Constraints
// ---------------------------------------------------------------------------
constexpr ErrorDef<raw::Attribute> ErrInvalidAttributePlacement(
    "placement of attribute '{}' disallowed here");
constexpr ErrorDef<raw::Attribute, std::string, std::set<std::string>> ErrInvalidAttributeValue(
    "attribute '{}' has invalid value '{}', should be one of '{}'");
constexpr ErrorDef<raw::Attribute, std::string> ErrAttributeConstraintNotSatisfied(
    "declaration did not satisfy constraint of attribute '{}' with value '{}'");
constexpr ErrorDef<flat::Name> ErrUnionCannotBeSimple("union '{}' is not allowed to be simple");
constexpr ErrorDef<std::string_view> ErrMemberMustBeSimple("member '{}' is not simple");
constexpr ErrorDef<uint32_t, uint32_t> ErrTooManyBytes(
    "too large: only {} bytes allowed, but {} bytes found");
constexpr ErrorDef<uint32_t, uint32_t> ErrTooManyHandles(
    "too many handles: only {} allowed, but {} found");
constexpr ErrorDef ErrInvalidErrorType(
    "invalid error type: must be int32, uint32 or an enum therof");
constexpr ErrorDef<std::string, std::set<std::string>> ErrInvalidTransportType(
    "invalid transport type: got {} expected one of {}");
constexpr ErrorDef ErrBoundIsTooBig("bound is too big");
constexpr ErrorDef<std::string> ErrUnableToParseBound("unable to parse bound '{}'");
constexpr WarningDef<std::string, std::string> WarnAttributeTypo(
    "suspect attribute with name '{}'; did you mean '{}'?");

// ---------------------------------------------------------------------------
// Type Templates
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name> ErrUnknownType("unknown type {}");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustBeAProtocol("{} must be a protocol");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotUseServicesInOtherDeclarations(
    "{} cannot use services in other declarations");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotParametrizeTwice(
    "{} cannot parametrize twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBoundTwice("{} cannot bound twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotIndicateNullabilityTwice(
    "{} cannot indicate nullability twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustBeParameterized("{} must be parametrized");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustHaveSize("{} must have size");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustHaveNonZeroSize("{} must have non-zero size");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBeParameterized(
    "{} cannot be parametrized");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotHaveSize("{} cannot have size");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBeNullable("{} cannot be nullable");

constexpr ErrorDef<std::vector<std::string_view>, std::vector<std::string_view>,
                   std::vector<std::string_view>>
    ErrUnusedImport("Library {} imports {} but does not use it. Either use {}, or remove import.");

}  // namespace diagnostics
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTICS_H_
