/*
 * CORBA echo tests
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Author: Elliot Lee <sopwith@redhat.com>
 */

/*
 * from main program
 */
extern gboolean		echo_opt_quiet;
/*
 * from echo-srv.c 
 */
extern void		echo_srv_start_poa(CORBA_ORB orb,CORBA_Environment *ev);
extern CORBA_Object	echo_srv_start_object(CORBA_Environment *ev);
extern void		echo_srv_finish_object(CORBA_Environment *ev);
extern void		echo_srv_finish_poa(CORBA_Environment *ev);
