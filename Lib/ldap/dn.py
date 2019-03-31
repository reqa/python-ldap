"""
dn.py - misc stuff for handling distinguished names (see RFC 4514)

See https://www.python-ldap.org/ for details.
"""

import sys
from ldap.pkginfo import __version__

import _ldap
assert _ldap.__version__==__version__, \
       ImportError('ldap %s and _ldap %s version mismatch!' % (__version__,_ldap.__version__))

import ldap.functions


def escape_dn_chars(s):
  """
  Escape all DN special characters found in s
  with a back-slash (see RFC 4514, section 2.4)
  """
  if s:
    s = s.replace('\\','\\\\')
    s = s.replace(',' ,'\\,')
    s = s.replace('+' ,'\\+')
    s = s.replace('"' ,'\\"')
    s = s.replace('<' ,'\\<')
    s = s.replace('>' ,'\\>')
    s = s.replace(';' ,'\\;')
    s = s.replace('=' ,'\\=')
    s = s.replace('\000' ,'\\\000')
    if s[0]=='#' or s[0]==' ':
      s = ''.join(('\\',s))
    if s[-1]==' ':
      s = ''.join((s[:-1],'\\ '))
  return s


def str2dn(dn,flags=0):
  """
  This function takes a DN as string as parameter and returns
  a decomposed DN. It's the inverse to dn2str().

  flags describes the format of the dn

  See also the OpenLDAP man-page ldap_str2dn(3)
  """
  if not dn:
    return []
  if sys.version_info[0] < 3 and isinstance(dn, unicode):
      dn = dn.encode('utf-8')
  return ldap.functions._ldap_function_call(None,_ldap.str2dn,dn,flags)


def dn2str(dn):
  """
  This function takes a decomposed DN as parameter and returns
  a single string. It's the inverse to str2dn() but will always
  return a DN in LDAPv3 format compliant to RFC 4514.
  """
  return ','.join([
    '+'.join([
      '='.join((atype,escape_dn_chars(avalue or '')))
      for atype,avalue,dummy in rdn])
    for rdn in dn
  ])

def explode_dn(dn, notypes=False, flags=0):
  """
  explode_dn(dn [, notypes=False [, flags=0]]) -> list

  This function takes a DN and breaks it up into its component parts.
  The notypes parameter is used to specify that only the component's
  attribute values be returned and not the attribute types.
  """
  if not dn:
    return []
  dn_decomp = str2dn(dn,flags)
  rdn_list = []
  for rdn in dn_decomp:
    if notypes:
      rdn_list.append('+'.join([
        escape_dn_chars(avalue or '')
        for atype,avalue,dummy in rdn
      ]))
    else:
      rdn_list.append('+'.join([
        '='.join((atype,escape_dn_chars(avalue or '')))
        for atype,avalue,dummy in rdn
      ]))
  return rdn_list


def explode_rdn(rdn, notypes=False, flags=0):
  """
  explode_rdn(rdn [, notypes=0 [, flags=0]]) -> list

  This function takes a RDN and breaks it up into its component parts
  if it is a multi-valued RDN.
  The notypes parameter is used to specify that only the component's
  attribute values be returned and not the attribute types.
  """
  if not rdn:
    return []
  rdn_decomp = str2dn(rdn,flags)[0]
  if notypes:
    return [avalue or '' for atype,avalue,dummy in rdn_decomp]
  else:
    return ['='.join((atype,escape_dn_chars(avalue or ''))) for atype,avalue,dummy in rdn_decomp]


def is_dn(s,flags=0):
  """
  Returns True is `s' can be parsed by ldap.dn.str2dn() like as a
  distinguished host_name (DN), otherwise False is returned.
  """
  try:
    str2dn(s,flags)
  except Exception:
    return False
  else:
    return True


def compare_dn(a, b):
    """
    Compares two distinguished names and return True if they are equal.
    """
    case_insensitive_attributes = [attr.lower() for attr in compare_dn.case_insensitive_attributes]

    def normalize(attr, value):
        if attr.lower() in case_insensitive_attributes:
            value = value.lower()
        return value
    return [sorted((x.lower(), normalize(x, y), z) for x, y, z in rdn) for rdn in ldap.dn.str2dn(a)] == [sorted((x.lower(), normalize(x, y), z) for x, y, z in rdn) for rdn in ldap.dn.str2dn(b)]


compare_dn.case_insensitive_attributes = []


def initialize_case_insensitive_attributes(lo):
    """
    Compares two distinguished names with respect to their attribute case sensitivy matching rule and return True if they are equal.
    """
    from ldap.schema import (SubSchema, ObjectClass, AttributeType)
    subschema = SubSchema(lo.read_subschemasubentry_s(lo.search_subschemasubentry_s()), 0)
    attrs = set()
    for oid in subschema.listall(AttributeType):
        attr = subschema.get_obj(AttributeType, oid)
        if attr.equality and attr.equality.startswith('caseIgnore'):
            attrs |= set(attr.names)
    compare_dn.case_insensitive_attributes.extend(attrs)
