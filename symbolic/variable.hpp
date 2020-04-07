﻿#pragma once
#define SYMEX_CONST_SIZE_DEFAULT(x)		0

#include <string>
#include <codecvt>
#include <type_traits>
#include "..\arch\operands.hpp"
#include "..\arch\instruction_set.hpp"
#include "..\routine\basic_block.hpp"
#include "..\misc\format.hpp"

namespace vtil::symbolic
{
	// Dictionary for the names of reserved unique identifiers. (For simplifier)
	//
	static const std::wstring uid_reserved_dictionary =
		L"ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩαβγδεζηθικλμνξοπρστυφχψω"
		L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	// Unique identifier for a variable.
	//
	struct unique_identifier
	{
		// Conversion between the internal unicode type and the UTF8 output expected.
		//
		using utf_cvt_t = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>;

		// Unique name of the variable.
		//
		std::string name;

		// Origin of the variable if relevant.
		//
		ilstream_const_iterator origin = {};
		std::optional<register_view> register_id = {};
		std::optional<std::pair<register_view, int64_t>> memory_id = {};
		uint32_t memory_base_idx = 0;
		bool branch_dependant = false;

		// Default constructors for simple identifiers.
		//
		unique_identifier() {}
		unique_identifier( const std::string& name ) : name( name ) { fassert( is_valid() ); }

		// Constructors for unique identifiers created from stream iterators.
		//
		unique_identifier( const register_view& base, int64_t offset, ilstream_const_iterator at )
		{
			assign( base, offset ).bind( at );
		}
		unique_identifier( const register_view& reg, ilstream_const_iterator at )
		{
			assign( reg ).bind( at );
		}
		unique_identifier( ilstream_const_iterator origin, int operand_index )
		{
			// Identifier for memory:
			//
			if ( operand_index == -1 )
			{
				fassert( origin->base->accesses_memory() );
				auto [mem_base, mem_loc] = origin->get_mem_loc();
				assign( mem_base, mem_loc ).bind( origin );
			}
			// Identifier for register/temporary:
			//
			else
			{
				fassert( origin->operands[ operand_index ].is_register() );
				assign( origin->operands[ operand_index ].reg ).bind( origin );
			}
		}

		// Refreshes the unique identifier if it's bound to a register value or a pointer.
		//
		unique_identifier& refresh()
		{
			if ( register_id.has_value() )
			{
				name = register_id->to_string();

				if ( !origin.is_end() )
				{
					name += '@';
					if ( origin->vip != invalid_vip )
						name += format::str( "%llx", origin->vip );
					else
						name += format::str( "%llx", origin.container->entry_vip ) + "#" + std::to_string( std::distance( origin.container->begin(), origin ) );
				}
				else
				{
					name += origin.is_begin() ? "?" : "*";
				}
			}
			else if( memory_id.has_value() )
			{
				if ( memory_id->first.base == X86_REG_RSP )
				{
					name = memory_id->second >= 0 ? "arg" : "var";
					name += format::hex( abs( memory_id->second ) );
					if( memory_base_idx != 0 )
						name += format::str( "#%d", memory_base_idx );
				}
				else
				{
					name = "[";
					name += memory_id->first.base.to_string();
					name +=  "+" + format::hex( memory_id->second ) + "]";
				}

				if ( !origin.is_end() )
				{
					name += '@';
					if ( origin->vip != invalid_vip )
						name += format::str( "%llx", origin->vip );
					else
						name += format::str( "%llx", origin.container->entry_vip ) + "#" + std::to_string( std::distance( origin.container->begin(), origin ) );
				}
				else
				{
					name += origin.is_begin() ? "?" : "*";
				}
			}
			if(  branch_dependant )
				name += "...";
			return *this;
		}

		// Assigns unique identifier the value of a register or a pointer.
		//
		unique_identifier& assign( const register_view& reg )
		{
			register_id = { reg };
			memory_id = {};
			return refresh();
		}
		unique_identifier& assign( const register_view& base, int64_t offset )
		{
			register_id = {};
			memory_id = { base, offset };
			return refresh();
		}

		// Binds/unbinds the identifier from/to the origin.
		//
		unique_identifier& unbind() { origin = {}; return refresh(); }
		unique_identifier& bind( ilstream_const_iterator it )
		{ 
			origin = it;
			if ( origin.is_valid() && memory_id )
				memory_base_idx = origin.is_end() ? origin.container->sp_index : origin->sp_index;
			return refresh(); 
		}

		// Declares value branch dependant/not.
		//
		unique_identifier& set_branch_dependency( bool v = true )
		{
			if ( branch_dependant != v )
			{
				branch_dependant = v;
				return refresh();
			}
			return *this;
		}

		// Helpers used to resolve the actual operand being traced.
		//
		bool is_valid() const { return name.size() && !iswdigit( name[ 0 ] ); }
		bool is_reserved() const { return name.size() == 1 && uid_reserved_dictionary.find( name[ 0 ] ) != std::wstring::npos; }
		bool is_memory() const { return is_valid() && memory_id.has_value(); }
		bool is_register() const { return is_valid() && register_id.has_value(); }
		bool is_arbitrary() const { return is_valid() && ( memory_id.has_value() || register_id.has_value() ); }
		
		// Returns the pointer associated with the variable.
		//
		auto get_mem() const
		{
			fassert( is_memory() );
			return memory_id.value();
		}

		// Returns the register that is associated with the variable.
		//
		register_view get_reg() const
		{
			fassert( is_register() );
			return register_id.value();
		}

		// Conversion to human readable format.
		//
		std::string to_string() const { return name; }
	
		// Simple comparison operators.
		//
		bool operator==( const unique_identifier& o ) const 
		{ 
			return name == o.name && origin == o.origin;
		}
		bool operator<( const unique_identifier& o ) const { return name < o.name; }
		bool operator!=( const unique_identifier& o ) const { return name != o.name; }
	};

	// Describes a variable that will be used in a symbolic expression.
	//
	struct variable
	{
		// A unique identifier for the variable, if left empty
		// this variable will be treated as a constant value.
		//
		unique_identifier uid;

		// If constant, the value that is being represented
		// by this variable. Do not ever access directly (not via .get).
		//
		union
		{
			uint64_t _u64;
			int64_t _i64;
		};

		// Size of the variable, used for both constants and UID-bound variables.
		// - If zero, implies any size.
		//
		uint8_t size;

		// Default constructor, will make an invalid varaible.
		//
		variable() : size( 0 ) {}

		// Constructor for uniquely variables.
		//
		variable( const std::string& uid, uint8_t size ) : uid( uid ), size( size ) { fassert( is_valid() ); }
		variable( const unique_identifier& uid, uint8_t size ) : uid( uid ), size( size ) { fassert( is_valid() ); }
		variable( ilstream_const_iterator origin, int operand_index )
		{ 
			// If operand is an immediate or a register:
			//
			if ( operand_index != -1 )
			{
				// If immediate, assign constant:
				//
				if ( origin->operands[ operand_index ].is_immediate() )
					_u64 = origin->operands[ operand_index ].u64;
				// If register, generate unique identifier:
				//
				else
					uid = { origin, operand_index };

				// Assing operand size as the variable size.
				//
				size = origin->operands[ operand_index ].size();
			}
			// If operand is a stack pointer:
			//
			else
			{
				// Generate a unique identifier and assign the size.
				//
				uid = { origin, operand_index };
				size = origin->access_size();
			}
			
			fassert( is_valid() ); 
		}

		// Constructor for variables that represent constant values.
		//
		template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
		variable( T imm, uint8_t size = SYMEX_CONST_SIZE_DEFAULT( T ) ) : size( size )
		{
			fassert( is_valid() ); 

			// If a signed type was passed, sign extend, otherwise zero extend before storing.
			//
			if constexpr ( std::is_signed_v<T> )
				_i64 = imm;
			else
				_u64 = imm;
		}

		// Simple helpers to determine the type of the variable.
		//
		bool is_valid() const { return size == 0 || size == 1 || size == 2 || size == 4 || size == 8; }
		bool is_symbolic() const { return uid.is_valid(); }
		bool is_constant() const { return !uid.is_valid(); }

		// Wrappers around unique_identifer:: helpers used to resolve the actual operand being traced.
		//
		bool is_memory() const { return uid.is_memory(); }
		bool is_register() const { return uid.is_register(); }
		bool is_arbitrary() const { return uid.is_arbitrary(); }
		auto get_mem() const { return uid.get_mem(); }
		register_view get_reg() const { register_view reg = uid.get_reg(); fassert( size == reg.size ); return reg; }
		auto& unbind() { uid.unbind(); return *this; }
		auto& bind( ilstream_const_iterator it ) { uid.bind( it ); return *this; }
		
		// Resizes the variable.
		//
		variable& resize( uint8_t new_size, bool sign_extend )
		{
			if ( is_constant() )
				_u64 = sign_extend ? get<true>( new_size ) : get<false>( new_size );
			else
				fassert( size >= new_size && new_size != 0 );

			size = new_size;
			if ( is_register() ) uid.register_id->size = new_size;
			fassert( is_valid() );
			return *this;
		}

		// Instead of using the size variable as is, this function 
		// calculates minimum equivalent size if the constant is an
		// any-size special.
		//
		uint8_t calc_size( bool sign ) const
		{
			if ( size || !is_constant() ) 
				return size;
			
			if ( sign )
			{
				if ( get<true>( 1 ) == _i64 )		return 1;
				else if ( get<true>( 2 ) == _i64 )	return 2;
				else if ( get<true>( 4 ) == _i64 )	return 4;
				else if ( get<true>( 8 ) == _i64 )	return 8;
			}
			else
			{
				if ( get( 1 ) == _u64 )				return 1;
				else if ( get( 2 ) == _u64 )		return 2;
				else if ( get( 4 ) == _u64 )		return 4;
				else if ( get( 8 ) == _u64 )		return 8;
			} 
			unreachable();
		}

		// Getter for value:
		//
		template<bool sign = false>
		auto get( uint8_t new_size = 0 ) const
		{
			fassert( is_constant() );

			uint8_t out_size = size;
			if ( out_size == 0 || ( new_size != 0 && new_size < out_size ) )
				out_size = new_size;

			if constexpr ( sign )
			{
				switch ( out_size )
				{
					case 0: case 8: return _i64;						break;
					case 1: return ( int64_t ) *( int8_t* ) &_i64;		break;
					case 2: return ( int64_t ) *( int16_t* ) &_i64;		break;
					case 4: return ( int64_t ) *( int32_t* ) &_i64;		break;
				}
			}
			else
			{
				switch ( out_size )
				{
					case 0: case 8: return _u64;						break;
					case 1: return ( uint64_t ) *( uint8_t* ) &_u64;	break;
					case 2: return ( uint64_t ) *( uint16_t* ) &_u64;	break;
					case 4: return ( uint64_t ) *( uint32_t* ) &_u64;	break;
				}
			}
			unreachable();
		}

		// Converts the variable into human-readable format.
		//
		std::string to_string() const
		{
			if ( !is_symbolic() )
				return format::hex( get<true>() );
			else if ( size == 0 )
				return uid.to_string();
			else
				return uid.to_string() + format::suffix_map[ size ];
		}

		// Basic comparison operators.
		//
		bool operator==( const variable& o ) const 
		{
			if ( is_constant() )
				return o.is_constant() && get( o.size ) == o.get( size );
			else
				return o.is_symbolic() && uid == o.uid && size == o.size;
		}
		bool operator!=( const variable& o ) const { return !operator==( o ); }
		bool operator<( const variable& o ) const 
		{ 
			if ( is_symbolic() != o.is_symbolic() )
				return o.is_symbolic();

			if ( is_symbolic() )
				return uid < o.uid;
			else
				return get() < o.get();
		}
	};
};