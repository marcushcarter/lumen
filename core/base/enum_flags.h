#pragma once
#include <type_traits>

#define LUMEN_ENUM_FLAGS(m_type) \
    constexpr m_type operator|(m_type a, m_type b) { return (m_type)((std::underlying_type_t<m_type>)a | (std::underlying_type_t<m_type>)b); } \
    constexpr m_type operator&(m_type a, m_type b) { return (m_type)((std::underlying_type_t<m_type>)a & (std::underlying_type_t<m_type>)b); } \
    constexpr m_type operator~(m_type a) { return (m_type)(~(std::underlying_type_t<m_type>)a); } \
    constexpr m_type& operator|=(m_type& a, m_type b) { return a = a | b; } \
    constexpr m_type& operator&=(m_type& a, m_type b) { return a = a & b; } \
    constexpr bool has_flag(m_type p_flags, m_type p_flag) { return ((std::underlying_type_t<m_type>)p_flags & (std::underlying_type_t<m_type>)p_flag) != 0; }
