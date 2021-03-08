/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*            Mark Shinwell and Leo White, Jane Street Europe             */
/*                                                                        */
/*   Copyright 2013--2016, Jane Street Group, LLC                         */
/*                                                                        */
/*   All rights reserved.  This file is distributed under the terms of    */
/*   the GNU Lesser General Public License version 2.1, with the          */
/*   special exception on linking described in the file LICENSE.          */
/*                                                                        */
/**************************************************************************/

#define CAML_INTERNALS

#include "caml/fail.h"
#include "caml/mlvalues.h"
#include "caml/io.h"

CAMLprim value caml_spacetime_only_works_for_native_code(value foo, ...)
{
  caml_failwith("Spacetime profiling only works for native code");
}

CAMLprim value caml_spacetime_enabled (value v_unit)
{
  return Val_false;  /* running in bytecode */
}

CAMLprim value caml_register_channel_for_spacetime (value v_channel)
{
  return Val_unit;
}
