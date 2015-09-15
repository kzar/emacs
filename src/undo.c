/* undo handling for GNU Emacs.
   Copyright (C) 1990, 1993-1994, 2000-2015 Free Software Foundation,
   Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <http://www.gnu.org/licenses/>.  */


#include <config.h>

#include "lisp.h"
#include "character.h"
#include "buffer.h"
#include "commands.h"
#include "window.h"

/* Position of point last time we inserted a boundary.  */
static struct buffer *last_boundary_buffer;
static ptrdiff_t last_boundary_position;

/* The first time a command records something for undo.
   it also allocates the undo-boundary object
   which will be added to the list at the end of the command.
   This ensures we can't run out of space while trying to make
   an undo-boundary.  */
static Lisp_Object pending_boundary;

/* Record point as it was at beginning of this command (if necessary)
   and prepare the undo info for recording a change.
   PT is the position of point that will naturally occur as a result of the
   undo record that will be added just after this command terminates.  */

static void
record_point (ptrdiff_t pt)
{
  bool at_boundary;

  /* Don't record position of pt when undo_inhibit_record_point holds.  */
  if (undo_inhibit_record_point)
    return;

  /* Allocate a cons cell to be the undo boundary after this command.  */
  if (NILP (pending_boundary))
    pending_boundary = Fcons (Qnil, Qnil);

  if(NILP (Vundo_buffer_undoably_changed)){
    Fset (Qundo_buffer_undoably_changed,Qt);
    safe_run_hooks (Qundo_first_undoable_change_hook);
  }

  at_boundary = ! CONSP (BVAR (current_buffer, undo_list))
                || NILP (XCAR (BVAR (current_buffer, undo_list)));

  if (MODIFF <= SAVE_MODIFF)
    record_first_change ();

  /* If we are just after an undo boundary, and
     point wasn't at start of deleted range, record where it was.  */
  if (at_boundary
      && current_buffer == last_boundary_buffer
      && last_boundary_position != pt)
    bset_undo_list (current_buffer,
		    Fcons (make_number (last_boundary_position),
			   BVAR (current_buffer, undo_list)));
}

/* Record an insertion that just happened or is about to happen,
   for LENGTH characters at position BEG.
   (It is possible to record an insertion before or after the fact
   because we don't need to record the contents.)  */

void
record_insert (ptrdiff_t beg, ptrdiff_t length)
{
  Lisp_Object lbeg, lend;

  if (EQ (BVAR (current_buffer, undo_list), Qt))
    return;

  record_point (beg);

  /* If this is following another insertion and consecutive with it
     in the buffer, combine the two.  */
  if (CONSP (BVAR (current_buffer, undo_list)))
    {
      Lisp_Object elt;
      elt = XCAR (BVAR (current_buffer, undo_list));
      if (CONSP (elt)
	  && INTEGERP (XCAR (elt))
	  && INTEGERP (XCDR (elt))
	  && XINT (XCDR (elt)) == beg)
	{
	  XSETCDR (elt, make_number (beg + length));
	  return;
	}
    }

  XSETFASTINT (lbeg, beg);
  XSETINT (lend, beg + length);
  bset_undo_list (current_buffer,
		  Fcons (Fcons (lbeg, lend), BVAR (current_buffer, undo_list)));
}

/* Record the fact that markers in the region of FROM, TO are about to
   be adjusted.  This is done only when a marker points within text
   being deleted, because that's the only case where an automatic
   marker adjustment won't be inverted automatically by undoing the
   buffer modification.  */

static void
record_marker_adjustments (ptrdiff_t from, ptrdiff_t to)
{
  Lisp_Object marker;
  register struct Lisp_Marker *m;
  register ptrdiff_t charpos, adjustment;

  /* Allocate a cons cell to be the undo boundary after this command.  */
  if (NILP (pending_boundary))
    pending_boundary = Fcons (Qnil, Qnil);


  if(NILP (Vundo_buffer_undoably_changed)){
    Fset (Qundo_buffer_undoably_changed,Qt);
    safe_run_hooks (Qundo_first_undoable_change_hook);
  }

  for (m = BUF_MARKERS (current_buffer); m; m = m->next)
    {
      charpos = m->charpos;
      eassert (charpos <= Z);

      if (from <= charpos && charpos <= to)
        {
          /* insertion_type nil markers will end up at the beginning of
             the re-inserted text after undoing a deletion, and must be
             adjusted to move them to the correct place.

             insertion_type t markers will automatically move forward
             upon re-inserting the deleted text, so we have to arrange
             for them to move backward to the correct position.  */
          adjustment = (m->insertion_type ? to : from) - charpos;

          if (adjustment)
            {
              XSETMISC (marker, m);
              bset_undo_list
                (current_buffer,
                 Fcons (Fcons (marker, make_number (adjustment)),
                        BVAR (current_buffer, undo_list)));
            }
        }
    }
}

/* Record that a deletion is about to take place, of the characters in
   STRING, at location BEG.  Optionally record adjustments for markers
   in the region STRING occupies in the current buffer.  */

void
record_delete (ptrdiff_t beg, Lisp_Object string, bool record_markers)
{
  Lisp_Object sbeg;

  if (EQ (BVAR (current_buffer, undo_list), Qt))
    return;

  if (PT == beg + SCHARS (string))
    {
      XSETINT (sbeg, -beg);
      record_point (PT);
    }
  else
    {
      XSETFASTINT (sbeg, beg);
      record_point (beg);
    }

  /* primitive-undo assumes marker adjustments are recorded
     immediately before the deletion is recorded.  See bug 16818
     discussion.  */
  if (record_markers)
    record_marker_adjustments (beg, beg + SCHARS (string));

  bset_undo_list
    (current_buffer,
     Fcons (Fcons (string, sbeg), BVAR (current_buffer, undo_list)));
}

/* Record that a replacement is about to take place,
   for LENGTH characters at location BEG.
   The replacement must not change the number of characters.  */

void
record_change (ptrdiff_t beg, ptrdiff_t length)
{
  record_delete (beg, make_buffer_string (beg, beg + length, true), false);
  record_insert (beg, length);
}

/* Record that an unmodified buffer is about to be changed.
   Record the file modification date so that when undoing this entry
   we can tell whether it is obsolete because the file was saved again.  */

void
record_first_change (void)
{
  struct buffer *base_buffer = current_buffer;

  if (EQ (BVAR (current_buffer, undo_list), Qt))
    return;

  if (base_buffer->base_buffer)
    base_buffer = base_buffer->base_buffer;

  bset_undo_list (current_buffer,
		  Fcons (Fcons (Qt, Fvisited_file_modtime ()),
			 BVAR (current_buffer, undo_list)));
}

/* Record a change in property PROP (whose old value was VAL)
   for LENGTH characters starting at position BEG in BUFFER.  */

void
record_property_change (ptrdiff_t beg, ptrdiff_t length,
			Lisp_Object prop, Lisp_Object value,
			Lisp_Object buffer)
{
  Lisp_Object lbeg, lend, entry;
  struct buffer *obuf = current_buffer, *buf = XBUFFER (buffer);
  bool boundary = false;

  if (EQ (BVAR (buf, undo_list), Qt))
    return;

  /* Allocate a cons cell to be the undo boundary after this command.  */
  if (NILP (pending_boundary))
    pending_boundary = Fcons (Qnil, Qnil);

  if(NILP (Vundo_buffer_undoably_changed)){
    Fset (Qundo_buffer_undoably_changed,Qt);
    safe_run_hooks (Qundo_first_undoable_change_hook);
  }

  /* Switch temporarily to the buffer that was changed.  */
  current_buffer = buf;

  if (MODIFF <= SAVE_MODIFF)
    record_first_change ();

  XSETINT (lbeg, beg);
  XSETINT (lend, beg + length);
  entry = Fcons (Qnil, Fcons (prop, Fcons (value, Fcons (lbeg, lend))));
  bset_undo_list (current_buffer,
		  Fcons (entry, BVAR (current_buffer, undo_list)));

  current_buffer = obuf;
}

DEFUN ("undo-boundary", Fundo_boundary, Sundo_boundary, 0, 0, 0,
       doc: /* Mark a boundary between units of undo.
An undo command will stop at this point,
but another undo command will undo to the previous boundary.  */)
  (void)
{
  Lisp_Object tem;
  if (EQ (BVAR (current_buffer, undo_list), Qt))
    return Qnil;
  tem = Fcar (BVAR (current_buffer, undo_list));
  if (!NILP (tem))
    {
      /* One way or another, cons nil onto the front of the undo list.  */
      if (!NILP (pending_boundary))
	{
	  /* If we have preallocated the cons cell to use here,
	     use that one.  */
	  XSETCDR (pending_boundary, BVAR (current_buffer, undo_list));
	  bset_undo_list (current_buffer, pending_boundary);
	  pending_boundary = Qnil;
	}
      else
	bset_undo_list (current_buffer,
			Fcons (Qnil, BVAR (current_buffer, undo_list)));
    }
  last_boundary_position = PT;
  last_boundary_buffer = current_buffer;
  return Qnil;
}

DEFUN ("undo-size", Fundo_size, Sundo_size, 0, 1, 0,
       doc: /* Return the size of `buffer-undo-list'.

If n count till the end of the nth boundary, or the whole list iff n
is zero.

Returns nil if `buffer-undo-list' is t; that is there is no undo list.
Otherwise, returns the size of `buffer-undo-list' in bytes.*/)
     (Lisp_Object n)
{
  // we do not have an undo_list anyway
  if (EQ (BVAR (current_buffer, undo_list), Qt))
    return Qnil;

  Lisp_Object prev, next;
  EMACS_INT size_so_far, boundary_so_far, num;
  if(n){
    CHECK_NUMBER(n);
    num = XINT(n);
  }

  prev = Qnil;
  next = BVAR (current_buffer, undo_list);

  while(CONSP (next))
    {
      Lisp_Object elt;
      elt = XCAR (next);

      size_so_far += sizeof (struct Lisp_Cons);
      if (CONSP (elt))
        {
          // we have a boundary, so check we do not have too many
          if (XCAR (elt) == Qnil)
            {
              boundary_so_far = boundary_so_far + 1;
              if(boundary_so_far >= num)
                break;
            }

          if (STRINGP (XCAR (elt)))
            size_so_far += (sizeof (struct Lisp_String) - 1
                            + SCHARS (XCAR (elt)));
        }

      // and advance
      prev = next;
      next = XCDR (next);
    }

  return make_number (size_so_far);
 }

/* At garbage collection time, make an undo list shorter at the end,
   returning the truncated list.  How this is done depends on the
   variables undo-limit, undo-strong-limit and undo-outer-limit.
   In some cases this works by calling undo-outer-limit-function.  */

void
truncate_undo_list (struct buffer *b)
{
  Lisp_Object list;
  Lisp_Object prev, next, last_boundary;
  EMACS_INT size_so_far = 0;

  /* Make sure that calling undo-outer-limit-function
     won't cause another GC.  */
  ptrdiff_t count = inhibit_garbage_collection ();

  /* Make the buffer current to get its local values of variables such
     as undo_limit.  Also so that Vundo_outer_limit_function can
     tell which buffer to operate on.  */
  record_unwind_current_buffer ();
  set_buffer_internal (b);

  list = BVAR (b, undo_list);

  prev = Qnil;
  next = list;
  last_boundary = Qnil;

  /* If the first element is an undo boundary, skip past it.  */
  if (CONSP (next) && NILP (XCAR (next)))
    {
      /* Add in the space occupied by this element and its chain link.  */
      size_so_far += sizeof (struct Lisp_Cons);

      /* Advance to next element.  */
      prev = next;
      next = XCDR (next);
    }

  /* Always preserve at least the most recent undo record
     unless it is really horribly big.

     Skip, skip, skip the undo, skip, skip, skip the undo,
     Skip, skip, skip the undo, skip to the undo bound'ry.  */

  while (CONSP (next) && ! NILP (XCAR (next)))
    {
      Lisp_Object elt;
      elt = XCAR (next);

      /* Add in the space occupied by this element and its chain link.  */
      size_so_far += sizeof (struct Lisp_Cons);
      if (CONSP (elt))
	{
	  size_so_far += sizeof (struct Lisp_Cons);
	  if (STRINGP (XCAR (elt)))
	    size_so_far += (sizeof (struct Lisp_String) - 1
			    + SCHARS (XCAR (elt)));
	}

      /* Advance to next element.  */
      prev = next;
      next = XCDR (next);
    }

  /* If by the first boundary we have already passed undo_outer_limit,
     we're heading for memory full, so offer to clear out the list.  */
  if (INTEGERP (Vundo_outer_limit)
      && size_so_far > XINT (Vundo_outer_limit)
      && !NILP (Vundo_outer_limit_function))
    {
      Lisp_Object tem;

      /* Normally the function this calls is undo-outer-limit-truncate.  */
      tem = call1 (Vundo_outer_limit_function, make_number (size_so_far));
      if (! NILP (tem))
	{
	  /* The function is responsible for making
	     any desired changes in buffer-undo-list.  */
	  unbind_to (count, Qnil);
	  return;
	}
    }

  if (CONSP (next))
    last_boundary = prev;

  /* Keep additional undo data, if it fits in the limits.  */
  while (CONSP (next))
    {
      Lisp_Object elt;
      elt = XCAR (next);

      /* When we get to a boundary, decide whether to truncate
	 either before or after it.  The lower threshold, undo_limit,
	 tells us to truncate after it.  If its size pushes past
	 the higher threshold undo_strong_limit, we truncate before it.  */
      if (NILP (elt))
	{
	  if (size_so_far > undo_strong_limit)
	    break;
	  last_boundary = prev;
	  if (size_so_far > undo_limit)
	    break;
	}

      /* Add in the space occupied by this element and its chain link.  */
      size_so_far += sizeof (struct Lisp_Cons);
      if (CONSP (elt))
	{
	  size_so_far += sizeof (struct Lisp_Cons);
	  if (STRINGP (XCAR (elt)))
	    size_so_far += (sizeof (struct Lisp_String) - 1
			    + SCHARS (XCAR (elt)));
	}

      /* Advance to next element.  */
      prev = next;
      next = XCDR (next);
    }

  /* If we scanned the whole list, it is short enough; don't change it.  */
  if (NILP (next))
    ;
  /* Truncate at the boundary where we decided to truncate.  */
  else if (!NILP (last_boundary))
    XSETCDR (last_boundary, Qnil);
  /* There's nothing we decided to keep, so clear it out.  */
  else
    bset_undo_list (b, Qnil);

  unbind_to (count, Qnil);
}


void
syms_of_undo (void)
{
  DEFSYM (Qinhibit_read_only, "inhibit-read-only");
  DEFSYM (Qundo_first_undoable_change_hook, "undo-first-undoable-change-hook");
  DEFSYM (Qundo_buffer_undoably_changed, "undo-buffer-undoably-changed");

  /* Marker for function call undo list elements.  */
  DEFSYM (Qapply, "apply");

  pending_boundary = Qnil;
  staticpro (&pending_boundary);

  last_boundary_buffer = NULL;

  defsubr (&Sundo_size);
  defsubr (&Sundo_boundary);

  DEFVAR_INT ("undo-limit", undo_limit,
	      doc: /* Keep no more undo information once it exceeds this size.
This limit is applied when garbage collection happens.
When a previous command increases the total undo list size past this
value, the earlier commands that came before it are forgotten.

The size is counted as the number of bytes occupied,
which includes both saved text and other data.  */);
  undo_limit = 80000;

  DEFVAR_INT ("undo-strong-limit", undo_strong_limit,
	      doc: /* Don't keep more than this much size of undo information.
This limit is applied when garbage collection happens.
When a previous command increases the total undo list size past this
value, that command and the earlier commands that came before it are forgotten.
However, the most recent buffer-modifying command's undo info
is never discarded for this reason.

The size is counted as the number of bytes occupied,
which includes both saved text and other data.  */);
  undo_strong_limit = 120000;

  DEFVAR_LISP ("undo-outer-limit", Vundo_outer_limit,
	      doc: /* Outer limit on size of undo information for one command.
At garbage collection time, if the current command has produced
more than this much undo information, it discards the info and displays
a warning.  This is a last-ditch limit to prevent memory overflow.

The size is counted as the number of bytes occupied, which includes
both saved text and other data.  A value of nil means no limit.  In
this case, accumulating one huge undo entry could make Emacs crash as
a result of memory overflow.

In fact, this calls the function which is the value of
`undo-outer-limit-function' with one argument, the size.
The text above describes the behavior of the function
that variable usually specifies.  */);
  Vundo_outer_limit = make_number (12000000);

  DEFVAR_LISP ("undo-outer-limit-function", Vundo_outer_limit_function,
	       doc: /* Function to call when an undo list exceeds `undo-outer-limit'.
This function is called with one argument, the current undo list size
for the most recent command (since the last undo boundary).
If the function returns t, that means truncation has been fully handled.
If it returns nil, the other forms of truncation are done.

Garbage collection is inhibited around the call to this function,
so it must make sure not to do a lot of consing.  */);
  Vundo_outer_limit_function = Qnil;

  DEFVAR_BOOL ("undo-inhibit-record-point", undo_inhibit_record_point,
	       doc: /* Non-nil means do not record `point' in `buffer-undo-list'.  */);
  undo_inhibit_record_point = false;

  DEFVAR_LISP ("undo-first-undoable-change-hook",
               Vundo_first_undoable_change_hook,
               doc: /* Normal hook run when a buffer has its first recent undo-able change.

This hook will be run with `current-buffer' as the buffer that
has changed. Recent means since the value of `undo-buffer-undoably-changed'
was last set of nil. */);
  Vundo_first_undoable_change_hook = Qnil;

  DEFVAR_LISP ("undo-buffer-undoably-changed",
               Vundo_buffer_undoably_changed,
               doc: /* Non-nil means that that the buffer has had a recent undo-able change.

Recent means since the value of this variable was last set explicitly to nil,
usually as part of the undo machinary.*/);

  Fmake_variable_buffer_local (Qundo_buffer_undoably_changed);
}
