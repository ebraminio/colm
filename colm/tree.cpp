/*
 *  Copyright 2008 Adrian Thurston <thurston@complang.org>
 */

/*  This file is part of Colm.
 *
 *  Colm is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  Colm is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with Colm; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include "bytecode.h"
#include "pdarun.h"
#include "fsmrun.h"
#include "pdarun.h"
#include <iostream>

using std::cout;
using std::cerr;
using std::endl;
using std::ostream;

Kid *alloc_attrs( Program *prg, long length )
{
	Kid *cur = 0;
	for ( long i = 0; i < length; i++ ) {
		Kid *next = cur;
		cur = prg->kidPool.allocate();
		cur->next = next;
	}
	return cur;
}

void free_attrs( Program *prg, Kid *attrs )
{
	Kid *cur = attrs;
	while ( cur != 0 ) {
		Kid *next = cur->next;
		prg->kidPool.free( cur );
		cur = next;
	}
}

void set_attr( Tree *tree, long pos, Tree *val )
{
	Kid *kid = tree->child;

	if ( tree->flags & AF_LEFT_IGNORE )
		kid = kid->next;
	if ( tree->flags & AF_RIGHT_IGNORE )
		kid = kid->next;

	for ( long i = 0; i < pos; i++ )
		kid = kid->next;
	kid->tree = val;
}

Tree *get_attr( Tree *tree, long pos )
{
	Kid *kid = tree->child;

	if ( tree->flags & AF_LEFT_IGNORE )
		kid = kid->next;
	if ( tree->flags & AF_RIGHT_IGNORE )
		kid = kid->next;

	for ( long i = 0; i < pos; i++ )
		kid = kid->next;
	return kid->tree;
}

Kid *get_attr_kid( Tree *tree, long pos )
{
	Kid *kid = tree->child;

	if ( tree->flags & AF_LEFT_IGNORE )
		kid = kid->next;
	if ( tree->flags & AF_RIGHT_IGNORE )
		kid = kid->next;

	for ( long i = 0; i < pos; i++ )
		kid = kid->next;
	return kid;
}

Kid *kid_list_concat( Kid *list1, Kid *list2 )
{
	if ( list1 == 0 )
		return list2;
	else if ( list2 == 0 )
		return list1;

	Kid *dest = list1;
	while ( dest->next != 0 )
		dest = dest->next;
	dest->next = list2;
	return list1;
}


Stream *open_stream_file( Program *prg, FILE *file )
{
	Stream *res = (Stream*)prg->mapElPool.allocate();
	res->id = LEL_ID_STREAM;
	res->file = file;
	res->in = new InputStreamFile( file );
	initInputStream( res->in );
	return res;
}

Stream *open_stream_fd( Program *prg, long fd )
{
	Stream *res = (Stream*)prg->mapElPool.allocate();
	res->id = LEL_ID_STREAM;
	res->in = new InputStreamFd( fd );
	initInputStream( res->in );
	return res;
}

Stream *open_file( Program *prg, Tree *name, Tree *mode )
{
	Head *headName = ((Str*)name)->value;
	Head *headMode = ((Str*)mode)->value;

	const char *givenMode = string_data(headMode);
	const char *fopenMode = 0;
	if ( memcmp( givenMode, "r", string_length(headMode) ) == 0 )
		fopenMode = "rb";
	else if ( memcmp( givenMode, "w", string_length(headMode) ) == 0 )
		fopenMode = "wb";
	else {
		cerr << "unknown file open mode: " << givenMode << endl;
		exit(1);
	}
	
	/* Need to make a C-string (null terminated). */
	char *fileName = new char[string_length(headName)+1];
	memcpy( fileName, string_data(headName), string_length(headName) );
	fileName[string_length(headName)] = 0;
	FILE *file = fopen( fileName, fopenMode );
	delete[] fileName;
	return open_stream_file( prg, file );
}

Tree *construct_integer( Program *prg, long i )
{
	Int *integer = (Int*) prg->treePool.allocate();
	integer->id = LEL_ID_INT;
	integer->value = i;

	return (Tree*)integer;
}

Tree *construct_string( Program *prg, Head *s )
{
	Str *str = (Str*) prg->treePool.allocate();
	str->id = LEL_ID_STR;
	str->value = s;

	return (Tree*)str;
}

Tree *construct_pointer( Program *prg, Tree *tree )
{
	Kid *kid = prg->kidPool.allocate();
	kid->tree = tree;
	kid->next = prg->heap;
	prg->heap = kid;

	Pointer *pointer = (Pointer*) prg->treePool.allocate();
	pointer->id = LEL_ID_PTR;
	pointer->value = kid;
	
	return (Tree*)pointer;
}

Tree *construct_term( Program *prg, Word id, Head *tokdata )
{
	LangElInfo *lelInfo = prg->rtd->lelInfo;

	Tree *tree = prg->treePool.allocate();
	tree->id = id;
	tree->refs = 0;
	tree->tokdata = tokdata;

	int objectLength = lelInfo[tree->id].objectLength;
	tree->child = alloc_attrs( prg, objectLength );

	return tree;
}

Kid *construct_replacement_kid( Tree **bindings, Program *prg, Kid *prev, long pat );

Kid *construct_ignore_list( Program *prg, long pat )
{
	PatReplNode *nodes = prg->rtd->patReplNodes;
	long ignore = nodes[pat].ignore;

	Kid *first = 0, *last = 0;
	while ( ignore >= 0 ) {
		Head *ignoreData = string_alloc_pointer( prg, nodes[ignore].data, nodes[ignore].length );

		Tree *ignTree = prg->treePool.allocate();
		ignTree->refs = 1;
		ignTree->id = nodes[ignore].id;
		ignTree->tokdata = ignoreData;

		Kid *ignKid = prg->kidPool.allocate();
		ignKid->tree = ignTree;
		ignKid->next = 0;

		if ( last == 0 )
			first = ignKid;
		else
			last->next = ignKid;

		ignore = nodes[ignore].next;
		last = ignKid;
	}

	return first;
}

/* Returns an uprefed tree. Saves us having to downref and bindings to zero to
 * return a zero-ref tree. */
Tree *construct_replacement_tree( Tree **bindings, Program *prg, long pat )
{
	PatReplNode *nodes = prg->rtd->patReplNodes;
	LangElInfo *lelInfo = prg->rtd->lelInfo;
	Tree *tree = 0;

	if ( nodes[pat].bindId > 0 ) {
		/* All bindings have been uprefed. */
		tree = bindings[nodes[pat].bindId];

		long ignore = nodes[pat].ignore;
		if ( ignore >= 0 ) {
			Kid *ignore = construct_ignore_list( prg, pat );

			tree = split_tree( prg, tree );

			Kid *ignoreHead = prg->kidPool.allocate();
			ignoreHead->next = tree->child;
			tree->child = ignoreHead;

			ignoreHead->tree = (Tree*) ignore;
			tree->flags |= AF_LEFT_IGNORE;
		}
	}
	else {
		tree = prg->treePool.allocate();
		tree->id = nodes[pat].id;
		tree->refs = 1;
		tree->tokdata = nodes[pat].length == 0 ? 0 :
				string_alloc_pointer( prg, 
				nodes[pat].data, nodes[pat].length );

		int objectLength = lelInfo[tree->id].objectLength;

		Kid *attrs = alloc_attrs( prg, objectLength );
		Kid *ignore = construct_ignore_list( prg, pat );
		Kid *child = construct_replacement_kid( bindings, prg, 
				0, nodes[pat].child );

		tree->child = kid_list_concat( attrs, child );
		if ( ignore != 0 ) {
			Kid *ignoreHead = prg->kidPool.allocate();
			ignoreHead->next = tree->child;
			tree->child = ignoreHead;

			ignoreHead->tree = (Tree*) ignore;
			tree->flags |= AF_LEFT_IGNORE;
		}

		for ( int i = 0; i < lelInfo[tree->id].numCaptureAttr; i++ ) {
			long ci = pat+1+i;
			CaptureAttr *ca = prg->rtd->captureAttr + lelInfo[tree->id].captureAttr + i;
			Tree *attr = prg->treePool.allocate();
			attr->id = nodes[ci].id;
			attr->refs = 1;
			attr->tokdata = nodes[ci].length == 0 ? 0 :
					string_alloc_pointer( prg, 
					nodes[ci].data, nodes[ci].length );

			set_attr( tree, ca->offset, attr );
		}
	}

	return tree;
}

Kid *construct_replacement_kid( Tree **bindings, Program *prg, Kid *prev, long pat )
{
	PatReplNode *nodes = prg->rtd->patReplNodes;
	Kid *kid = 0;

	if ( pat != -1 ) {
		kid = prg->kidPool.allocate();
		kid->tree = construct_replacement_tree( bindings, prg, pat );

		/* Recurse down next. */
		Kid *next = construct_replacement_kid( bindings, prg, 
				kid, nodes[pat].next );

		kid->next = next;
	}

	return kid;
}

Tree *make_token( Tree **root, Program *prg, long nargs )
{
	Tree **const sp = root;
	Tree **base = vm_ptop() + nargs;

	Int *idInt = (Int*)base[-1];
	Str *textStr = (Str*)base[-2];

	long id = idInt->value;
	Head *tokdata = string_copy( prg, textStr->value );

	LangElInfo *lelInfo = prg->rtd->lelInfo;
	Tree *tree;

	if ( lelInfo[id].ignore ) {
		tree = prg->treePool.allocate();
		tree->refs = 1;
		tree->id = id;
		tree->tokdata = tokdata;
	}
	else {
		long objectLength = lelInfo[id].objectLength;
		Kid *attrs = alloc_attrs( prg, objectLength );

		tree = prg->treePool.allocate();
		tree->id = id;
		tree->refs = 1;
		tree->tokdata = tokdata;

		tree->child = attrs;

		assert( nargs-2 <= objectLength );
		for ( long id = 0; id < nargs-2; id++ ) {
			set_attr( tree, id, base[-3-id] );
			tree_upref( get_attr( tree, id) );
		}
	}
	return tree;
}

Tree *make_tree( Tree **root, Program *prg, long nargs )
{
	Tree **const sp = root;
	Tree **base = vm_ptop() + nargs;

	Int *idInt = (Int*)base[-1];

	long id = idInt->value;
	LangElInfo *lelInfo = prg->rtd->lelInfo;

	Tree *tree = prg->treePool.allocate();
	tree->id = id;
	tree->refs = 1;

	long objectLength = lelInfo[id].objectLength;
	Kid *attrs = alloc_attrs( prg, objectLength );

	Kid *last = 0, *child = 0;
	for ( long id = 0; id < nargs-1; id++ ) {
		Kid *kid = prg->kidPool.allocate();
		kid->tree = base[-2-id];
		tree_upref( kid->tree );

		if ( last == 0 )
			child = kid;
		else
			last->next = kid;

		last = kid;
	}

	tree->child = kid_list_concat( attrs, child );

	return tree;
}

bool test_false( Program *prg, Tree *tree )
{
	bool flse = ( 
		tree == 0 ||
		tree == prg->falseVal ||
		( tree->id == LEL_ID_INT && ((Int*)tree)->value == 0 ) );
	return flse;
}

void print_str( ostream &out, Head *str )
{
	out.write( (char*)(str->data), str->length );
}

void print_str2( FILE *out, Head *str )
{
	fwrite( (char*)(str->data), str->length, 1, out );
}

/* Note that this function causes recursion, thought it is not a big
 * deal since the recursion it is only caused by nonterminals that are ignored. */
void print_ignore_list( ostream &out, Tree **sp, Program *prg, Tree *tree )
{
	Kid *ignore = tree_ignore( prg, tree );

	/* Record the root of the stack and push everything. */
	Tree **root = vm_ptop();
	while ( ignore != 0 ) {
		vm_push( (SW)ignore );
		ignore = ignore->next;
	}

	/* Pop them off and print. */
	while ( vm_ptop() != root ) {
		ignore = (Kid*) vm_pop();
		print_tree( out, sp, prg, ignore->tree );
	}
}

/* Note that this function causes recursion, thought it is not a big
 * deal since the recursion it is only caused by nonterminals that are ignored. */
void print_ignore_list2( FILE *out, Tree **sp, Program *prg, Tree *tree )
{
	Kid *ignore = tree_ignore( prg, tree );

	/* Record the root of the stack and push everything. */
	Tree **root = vm_ptop();
	while ( ignore != 0 ) {
		vm_push( (SW)ignore );
		ignore = ignore->next;
	}

	/* Pop them off and print. */
	while ( vm_ptop() != root ) {
		ignore = (Kid*) vm_pop();
		print_tree2( out, sp, prg, ignore->tree );
	}
}


void print_kid( ostream &out, Tree **&sp, Program *prg, Kid *kid, bool printIgnore )
{
	Tree **root = vm_ptop();
	Kid *child;

rec_call:
	/* If not currently skipping ignore data, then print it. Ignore data can
	 * be associated with terminals and nonterminals. */
	if ( printIgnore && tree_ignore( prg, kid->tree ) != 0 ) {
		/* Ignorelists are reversed. */
		print_ignore_list( out, sp, prg, kid->tree );
		printIgnore = false;
	}

	if ( kid->tree->id < prg->rtd->firstNonTermId ) {
		/* Always turn on ignore printing when we get to a token. */
		printIgnore = true;

		if ( kid->tree->id == LEL_ID_INT )
			out << ((Int*)kid->tree)->value;
		else if ( kid->tree->id == LEL_ID_BOOL ) {
			if ( ((Int*)kid->tree)->value )
				out << "true";
			else
				out << "false";
		}
		else if ( kid->tree->id == LEL_ID_PTR )
			out << '#' << (void*) ((Pointer*)kid->tree)->value;
		else if ( kid->tree->id == LEL_ID_STR )
			print_str( out, ((Str*)kid->tree)->value );
		else if ( kid->tree->id == LEL_ID_STREAM )
			out << '#' << (void*) ((Stream*)kid->tree)->file;
		else if ( kid->tree->tokdata != 0 && 
				string_length( kid->tree->tokdata ) > 0 )
		{
			out.write( string_data( kid->tree->tokdata ), 
					string_length( kid->tree->tokdata ) );
		}
	}
	else {
		/* Non-terminal. */
		child = tree_child( prg, kid->tree );
		if ( child != 0 ) {
			vm_push( (SW)kid );
			kid = child;
			while ( kid != 0 ) {
				goto rec_call;
				rec_return:
				kid = kid->next;
			}
			kid = (Kid*)vm_pop();
		}
	}

	if ( vm_ptop() != root )
		goto rec_return;
}

void print_tree( ostream &out, Tree **&sp, Program *prg, Tree *tree )
{
	if ( tree == 0 )
		out << "NIL";
	else {
		Kid kid;
		kid.tree = tree;
		kid.next = 0;
		print_kid( out, sp, prg, &kid, false );
	}
}


void print_kid2( FILE *out, Tree **&sp, Program *prg, Kid *kid, bool printIgnore )
{
	Tree **root = vm_ptop();
	Kid *child;

rec_call:
	/* If not currently skipping ignore data, then print it. Ignore data can
	 * be associated with terminals and nonterminals. */
	if ( printIgnore && tree_ignore( prg, kid->tree ) != 0 ) {
		/* Ignorelists are reversed. */
		print_ignore_list2( out, sp, prg, kid->tree );
		printIgnore = false;
	}

	if ( kid->tree->id < prg->rtd->firstNonTermId ) {
		/* Always turn on ignore printing when we get to a token. */
		printIgnore = true;

		if ( kid->tree->id == LEL_ID_INT )
			fprintf( out, "%ld", ((Int*)kid->tree)->value );
		else if ( kid->tree->id == LEL_ID_BOOL ) {
			if ( ((Int*)kid->tree)->value )
				fprintf( out, "true" );
			else
				fprintf( out, "false" );
		}
		else if ( kid->tree->id == LEL_ID_PTR )
			fprintf( out, "#%p", (void*) ((Pointer*)kid->tree)->value );
		else if ( kid->tree->id == LEL_ID_STR )
			print_str2( out, ((Str*)kid->tree)->value );
		else if ( kid->tree->id == LEL_ID_STREAM )
			fprintf( out, "#%p", ((Stream*)kid->tree)->file );
		else if ( kid->tree->tokdata != 0 && 
				string_length( kid->tree->tokdata ) > 0 )
		{
			fwrite( string_data( kid->tree->tokdata ), 
					string_length( kid->tree->tokdata ), 1, out );
		}
	}
	else {
		/* Non-terminal. */
		child = tree_child( prg, kid->tree );
		if ( child != 0 ) {
			vm_push( (SW)kid );
			kid = child;
			while ( kid != 0 ) {
				goto rec_call;
				rec_return:
				kid = kid->next;
			}
			kid = (Kid*)vm_pop();
		}
	}

	if ( vm_ptop() != root )
		goto rec_return;
}

void print_tree2( FILE *out, Tree **&sp, Program *prg, Tree *tree )
{
	if ( tree == 0 )
		fprintf( out, "NIL" );
	else {
		Kid kid;
		kid.tree = tree;
		kid.next = 0;
		print_kid2( out, sp, prg, &kid, false );
	}
}

void xml_escape_data( const char *data, long len )
{
	for ( int i = 0; i < len; i++ ) {
		if ( data[i] == '<' )
			cout << "&lt;";
		else if ( data[i] == '>' )
			cout << "&gt;";
		else if ( data[i] == '&' )
			cout << "&amp;";
		else if ( 32 <= data[i] && data[i] <= 126 )
			cout << data[i];
		else
			cout << "&#" << ((unsigned)data[i]) << ';';
	}
}

/* Might be a good idea to include this in the print_xml_kid function since
 * it is recursive and can eat up stac, however it's probably not a big deal
 * since the additional stack depth is only caused for nonterminals that are
 * ignored. */
void print_xml_ignore_list( Tree **sp, Program *prg, Tree *tree, long depth )
{
	Kid *ignore = tree_ignore( prg, tree );
	while ( ignore != 0 ) {
		print_xml_kid( sp, prg, ignore, true, depth );
		ignore = ignore->next;
	}
}

void print_xml_kid( Tree **&sp, Program *prg, Kid *kid, bool commAttr, int depth )
{
	Kid *child;
	Tree **root = vm_ptop();
	long i, objectLength;
	LangElInfo *lelInfo = prg->rtd->lelInfo;

	long kidNum = 0;;

rec_call:

	if ( kid->tree == 0 ) {
		for ( i = 0; i < depth; i++ )
			cout << "  ";

		cout << "NIL" << endl;
	}
	else {
		/* First print the ignore tokens. */
		if ( commAttr )
			print_xml_ignore_list( sp, prg, kid->tree, depth );

		for ( i = 0; i < depth; i++ )
			cout << "  ";

		/* Open the tag. Afterwards we will either print data underneath it or
		 * we will close it off immediately. */
		cout << '<' << lelInfo[kid->tree->id].name;

		/* If this is an attribute then give the node an attribute number. */
		if ( vm_ptop() != root ) {
			objectLength = lelInfo[((Kid*)vm_top())->tree->id].objectLength;
			if ( kidNum < objectLength )
				cout << " an=\"" << kidNum << '"';
		}

		objectLength = lelInfo[kid->tree->id].objectLength;
		child = tree_child( prg, kid->tree );
		if ( (commAttr && objectLength > 0) || child != 0 ) {
			cout << '>' << endl;
			vm_push( (SW) kidNum );
			vm_push( (SW) kid );

			kidNum = 0;
			kid = kid->tree->child;

			/* Skip over attributes if not printing comments and attributes. */
			if ( ! commAttr )
				kid = child;

			while ( kid != 0 ) {
				/* FIXME: I don't think we need this check for ignore any more. */
				if ( kid->tree == 0 || !lelInfo[kid->tree->id].ignore ) {
					depth++;
					goto rec_call;
					rec_return:
					depth--;
				}
				
				kid = kid->next;
				kidNum += 1;

				/* If the parent kid is a repeat then skip this node and go
				 * right to the first child (repeated item). */
				if ( lelInfo[((Kid*)vm_top())->tree->id].repeat )
					kid = kid->tree->child;

				/* If we have a kid and the parent is a list (recursive prod of
				 * list) then go right to the first child. */
				if ( kid != 0 && lelInfo[((Kid*)vm_top())->tree->id].list )
					kid = kid->tree->child;
			}

			kid = (Kid*) vm_pop();
			kidNum = (long) vm_pop();

			for ( i = 0; i < depth; i++ )
				cout << "  ";
			cout << "</" << lelInfo[kid->tree->id].name << '>' << endl;
		}
		else if ( kid->tree->id == LEL_ID_PTR ) {
			cout << '>' << (void*)((Pointer*)kid->tree)->value << 
					"</" << lelInfo[kid->tree->id].name << '>' << endl;
		}
		else if ( kid->tree->id == LEL_ID_BOOL ) {
			if ( ((Int*)kid->tree)->value )
				cout << ">true</";
			else
				cout << ">false</";
			cout << lelInfo[kid->tree->id].name << '>' << endl;
		}
		else if ( kid->tree->id == LEL_ID_INT ) {
			cout << '>' << ((Int*)kid->tree)->value << 
					"</" << lelInfo[kid->tree->id].name << '>' << endl;
		}
		else if ( kid->tree->id == LEL_ID_STR ) {
			Head *head = (Head*) ((Str*)kid->tree)->value;

			cout << '>';
			xml_escape_data( (char*)(head->data), head->length );
			cout << "</" << lelInfo[kid->tree->id].name << '>' << endl;
		}
		else if ( 0 < kid->tree->id && kid->tree->id < prg->rtd->firstNonTermId &&
				kid->tree->tokdata != 0 && 
				string_length( kid->tree->tokdata ) > 0 && 
				!lelInfo[kid->tree->id].literal )
		{
			cout << '>';
			xml_escape_data( string_data( kid->tree->tokdata ), 
					string_length( kid->tree->tokdata ) );
			cout << "</" << lelInfo[kid->tree->id].name << '>' << endl;
		}
		else
			cout << "/>" << endl;
	}

	if ( vm_ptop() != root )
		goto rec_return;
}

void print_xml_tree( Tree **&sp, Program *prg, Tree *tree, bool commAttr )
{
	Kid kid;
	kid.tree = tree;
	kid.next = 0;
	print_xml_kid( sp, prg, &kid, commAttr, 0 );
}

void stream_free( Program *prg, Stream *s )
{
	delete s->in;
	if ( s->file != 0 )
		fclose( s->file );
	prg->mapElPool.free( (MapEl*)s );
}

long tree_num_children( Program *prg, Tree *tree )
{
	long children = 0;
	Kid *child = tree_child( prg, tree );
	while ( child != 0 ) {
		children += 1;
		child = child->next;
	}

	return children;
}

Kid *copy_ignore_list( Program *prg, Kid *ignoreHeader )
{
	Kid *newHeader = prg->kidPool.allocate();
	Kid *last = 0, *ic = (Kid*)ignoreHeader->tree;
	while ( ic != 0 ) {
		Kid *newIc = prg->kidPool.allocate();

		newIc->tree = ic->tree;
		newIc->tree->refs += 1;

		/* List pointers. */
		if ( last == 0 )
			newHeader->tree = (Tree*)newIc;
		else
			last->next = newIc;

		ic = ic->next;
		last = newIc;
	}
	return newHeader;
}

/* New tree has zero ref. */
Tree *copy_real_tree( Program *prg, Tree *tree, Kid *oldNextDown, 
		Kid *&newNextDown, bool parseTree )
{
	/* Need to keep a lookout for next down. If 
	 * copying it, return the copy. */
	Tree *newTree;
	if ( parseTree ) {
		newTree = (Tree*) prg->parseTreePool.allocate();
		newTree->flags |= AF_PARSE_TREE;
	}
	else {
		newTree = prg->treePool.allocate();
	}

	newTree->id = tree->id;
	newTree->tokdata = string_copy( prg, tree->tokdata );

	/* Copy the child list. Start with ignores, then the list. */
	Kid *child = tree->child, *last = 0;

	/* Left ignores. */
	if ( tree->flags & AF_LEFT_IGNORE ) {
		newTree->flags |= AF_LEFT_IGNORE;
		Kid *newHeader = copy_ignore_list( prg, child );

		/* Always the head. */
		newTree->child = newHeader;

		child = child->next;
		last = newHeader;
	}

	/* Right ignores. */
	if ( tree->flags & AF_RIGHT_IGNORE ) {
		newTree->flags |= AF_RIGHT_IGNORE;
		Kid *newHeader = copy_ignore_list( prg, child );
		if ( last == 0 )
			newTree->child = newHeader;
		else
			last->next = newHeader;
		child = child->next;
		last = newHeader;
	}

	/* Attributes and children. */
	while ( child != 0 ) {
		Kid *newKid = prg->kidPool.allocate();

		/* Watch out for next down. */
		if ( child == oldNextDown )
			newNextDown = newKid;

		newKid->tree = child->tree;
		newKid->next = 0;

		/* May be an attribute. */
		if ( newKid->tree != 0 )
			newKid->tree->refs += 1;

		/* Store the first child. */
		if ( last == 0 )
			newTree->child = newKid;
		else
			last->next = newKid;

		child = child->next;
		last = newKid;
	}
	
	return newTree;
}

List *copy_list( Program *prg, List *list, Kid *oldNextDown, Kid *&newNextDown )
{
	#ifdef COLM_LOG_BYTECODE
	if ( colm_log_bytecode ) {
		cerr << "splitting list: " << list << " refs: " << 
				list->refs << endl;
	}
	#endif

	/* Not a need copy. */
	List *newList = (List*)prg->mapElPool.allocate();
	newList->id = list->genericInfo->langElId;
	newList->genericInfo = list->genericInfo;

	ListEl *src = list->head;
	while( src != 0 ) {
		ListEl *newEl = prg->listElPool.allocate();
		newEl->value = src->value;
		tree_upref( newEl->value );

		newList->append( newEl );

		/* Watch out for next down. */
		if ( (Kid*)src == oldNextDown )
			newNextDown = (Kid*)newEl;

		src = src->next;
	}

	return newList;
}
	
Map *copy_map( Program *prg, Map *map, Kid *oldNextDown, Kid *&newNextDown )
{
	#ifdef COLM_LOG_BYTECODE
	if ( colm_log_bytecode ) {
		cerr << "splitting map: " << map << " refs: " << 
				map->refs << endl;
	}
	#endif

	Map *newMap = (Map*)prg->mapElPool.allocate();
	newMap->id = map->genericInfo->langElId;
	newMap->genericInfo = map->genericInfo;
	newMap->treeSize = map->treeSize;
	newMap->root = 0;

	/* If there is a root, copy the tree. */
	if ( map->root != 0 ) {
		newMap->root = newMap->copyBranch( prg, map->root, 
				oldNextDown, newNextDown );
	}

	for ( MapEl *el = newMap->head; el != 0; el = el->next ) {
		assert( map->genericInfo->typeArg == TYPE_TREE );
		tree_upref( el->tree );
	}

	return newMap;
}

Tree *copy_tree( Program *prg, Tree *tree, Kid *oldNextDown, Kid *&newNextDown )
{
	LangElInfo *lelInfo = prg->rtd->lelInfo;
	long genericId = lelInfo[tree->id].genericId;
	if ( genericId > 0 ) {
		GenericInfo *generic = &prg->rtd->genericInfo[genericId];
		if ( generic->type == GEN_LIST )
			tree = (Tree*) copy_list( prg, (List*) tree, oldNextDown, newNextDown );
		else if ( generic->type == GEN_MAP )
			tree = (Tree*) copy_map( prg, (Map*) tree, oldNextDown, newNextDown );
		else if ( generic->type == GEN_PARSER ) {
			/* Need to figure out the semantics here. */
			cerr << "ATTEMPT TO COPY PARSER" << endl;
			assert(false);
		}
	}
	else if ( tree->id == LEL_ID_PTR )
		assert(false);
	else if ( tree->id == LEL_ID_BOOL )
		assert(false);
	else if ( tree->id == LEL_ID_INT )
		assert(false);
	else if ( tree->id == LEL_ID_STR )
		assert(false);
	else if ( tree->id == LEL_ID_STREAM )
		assert(false);
	else {
		tree = copy_real_tree( prg, tree, oldNextDown, newNextDown, false );
	}

	assert( tree->refs == 0 );
	return tree;
}

Tree *split_tree( Program *prg, Tree *tree )
{
	if ( tree != 0 ) {
		assert( tree->refs >= 1 );

		if ( tree->refs > 1 ) {
			#ifdef COLM_LOG_BYTECODE
			if ( colm_log_bytecode ) {
				cerr << "splitting tree: " << tree << " refs: " << 
						tree->refs << endl;
			}
			#endif

			Kid *oldNextDown = 0, *newNextDown = 0;
			Tree *newTree = copy_tree( prg, tree, oldNextDown, newNextDown );
			tree_upref( newTree );

			/* Downref the original. Don't need to consider freeing because
			 * refs were > 1. */
			tree->refs -= 1;

			tree = newTree;
		}

		assert( tree->refs == 1 );
	}
	return tree;
}

Tree *create_generic( Program *prg, long genericId )
{
	GenericInfo *genericInfo = &prg->rtd->genericInfo[genericId];
	Tree *newGeneric = 0;
	switch ( genericInfo->type ) {
		case GEN_MAP: {
			Map *map = (Map*)prg->mapElPool.allocate();
			map->id = genericInfo->langElId;
			map->genericInfo = genericInfo;
			newGeneric = (Tree*) map;
			break;
		}
		case GEN_LIST: {
			List *list = (List*)prg->mapElPool.allocate();
			list->id = genericInfo->langElId;
			list->genericInfo = genericInfo;
			newGeneric = (Tree*) list;
			break;
		}
		case GEN_PARSER: {
			Accum *accum = (Accum*)prg->mapElPool.allocate();
			accum->id = genericInfo->langElId;
			accum->genericInfo = genericInfo;
			accum->fsmRun = new FsmRun( prg );
			accum->pdaRun = new PdaRun( prg, prg->rtd->pdaTables, 
					accum->fsmRun, genericInfo->parserId, false, false );

			/* Start off the parsing process. */
			initPdaRun( accum->pdaRun, 0 );
			initFsmRun( accum->fsmRun );
			newToken( accum->pdaRun, accum->fsmRun );

			newGeneric = (Tree*) accum;
			break;
		}
		default:
			assert(false);
			return 0;
	}

	return newGeneric;
}


/* We can't make recursive calls here since the tree we are freeing may be
 * very large. Need the VM stack. */
void tree_free( Program *prg, Tree **sp, Tree *tree )
{
	Tree **top = sp;

free_tree:
	LangElInfo *lelInfo = prg->rtd->lelInfo;
	long genericId = lelInfo[tree->id].genericId;
	if ( genericId > 0 ) {
		GenericInfo *generic = &prg->rtd->genericInfo[genericId];
		if ( generic->type == GEN_LIST ) {
			List *list = (List*) tree;
			ListEl *el = list->head;
			while ( el != 0 ) {
				ListEl *next = el->next;
				vm_push( el->value );
				prg->listElPool.free( el );
				el = next;
			}
			prg->mapElPool.free( (MapEl*)list );
		}
		else if ( generic->type == GEN_MAP ) {
			Map *map = (Map*)tree;
			MapEl *el = map->head;
			while ( el != 0 ) {
				MapEl *next = el->next;
				vm_push( el->key );
				vm_push( el->tree );
				prg->mapElPool.free( el );
				el = next;
			}
			prg->mapElPool.free( (MapEl*)map );
		}
		else if ( generic->type == GEN_PARSER ) {
			Accum *accum = (Accum*)tree;
			delete accum->fsmRun;
			cleanParser( sp, accum->pdaRun );
			accum->pdaRun->clearContext( sp );
			rcode_downref_all( prg, sp, accum->pdaRun->allReverseCode );
			delete accum->pdaRun;
			tree_downref( prg, sp, (Tree*)accum->stream );
			prg->mapElPool.free( (MapEl*)accum );
		}
		else
			assert(false);
	}
	else {
		if ( tree->id == LEL_ID_STR ) {
			Str *str = (Str*) tree;
			string_free( prg, str->value );
			prg->treePool.free( tree );
		}
		else if ( tree->id == LEL_ID_BOOL || tree->id == LEL_ID_INT )
			prg->treePool.free( tree );
		else if ( tree->id == LEL_ID_PTR ) {
			//Pointer *ptr = (Pointer*)tree;
			//vm_push( ptr->value->tree );
			//prg->kidPool.free( ptr->value );
			prg->treePool.free( tree );
		}
		else if ( tree->id == LEL_ID_STREAM )
			stream_free( prg, (Stream*) tree );
		else { 
			string_free( prg, tree->tokdata );
			Kid *child = tree->child;

			/* Left ignore trees. */
			if ( tree->flags & AF_LEFT_IGNORE ) {
				Kid *ic = (Kid*)child->tree;
				while ( ic != 0 ) {
					Kid *next = ic->next;
					vm_push( ic->tree );
					prg->kidPool.free( ic );
					ic = next;
				}
			
				Kid *next = child->next;
				prg->kidPool.free( child );
				child = next;
			}

			/* Right ignore trees. */
			if ( tree->flags & AF_RIGHT_IGNORE ) {
				Kid *ic = (Kid*)child->tree;
				while ( ic != 0 ) {
					Kid *next = ic->next;
					vm_push( ic->tree );
					prg->kidPool.free( ic );
					ic = next;
				}

				Kid *next = child->next;
				prg->kidPool.free( child );
				child = next;
			}

			/* Attributes and grammar-based children. */
			while ( child != 0 ) {
				Kid *next = child->next;
				vm_push( child->tree );
				prg->kidPool.free( child );
				child = next;
			}

			if ( tree->flags & AF_PARSE_TREE )
				prg->parseTreePool.free( (ParseTree*)tree );
			else
				prg->treePool.free( tree );
		}
	}

	/* Any trees to downref? */
	while ( sp != top ) {
		tree = vm_pop();
		if ( tree != 0 ) {
			assert( tree->refs > 0 );
			tree->refs -= 1;
			if ( tree->refs == 0 )
				goto free_tree;
		}
	}
}

void tree_upref( Tree *tree )
{
	if ( tree != 0 )
		tree->refs += 1;
}

void tree_downref( Program *prg, Tree **sp, Tree *tree )
{
	if ( tree != 0 ) {
		assert( tree->refs > 0 );
		tree->refs -= 1;
		if ( tree->refs == 0 )
			tree_free( prg, sp, tree );
	}
}

/* Find the first child of a tree. */
Kid *tree_child( Program *prg, const Tree *tree )
{
	LangElInfo *lelInfo = prg->rtd->lelInfo;
	Kid *kid = tree->child;

	if ( tree->flags & AF_LEFT_IGNORE )
		kid = kid->next;
	if ( tree->flags & AF_RIGHT_IGNORE )
		kid = kid->next;

	/* Skip over attributes. */
	long objectLength = lelInfo[tree->id].objectLength;
	for ( long a = 0; a < objectLength; a++ )
		kid = kid->next;

	return kid;
}

/* Find the first child of a tree. */
Kid *tree_extract_child( Program *prg, Tree *tree )
{
	LangElInfo *lelInfo = prg->rtd->lelInfo;
	Kid *kid = tree->child, *last = 0;

	if ( tree->flags & AF_LEFT_IGNORE )
		kid = kid->next;
	if ( tree->flags & AF_RIGHT_IGNORE )
		kid = kid->next;

	/* Skip over attributes. */
	long objectLength = lelInfo[tree->id].objectLength;
	for ( long a = 0; a < objectLength; a++ ) {
		last = kid;
		kid = kid->next;
	}

	if ( last == 0 )
		tree->child = 0;
	else
		last->next = 0;

	return kid;
}


Kid *tree_ignore( Program *prg, Tree *tree )
{
	if ( tree->flags & AF_LEFT_IGNORE )
		return (Kid*)tree->child->tree;
	return 0;
}

Tree *tree_iter_deref_cur( TreeIter *iter )
{
	return iter->ref.kid == 0 ? 0 : iter->ref.kid->tree;
}

void ref_set_value( Ref *ref, Tree *v )
{
	Kid *firstKid = ref->kid;
	while ( ref != 0 && ref->kid == firstKid ) {
		ref->kid->tree = v;
		ref = ref->next;
	}
}

Tree *get_rhs_el( Program *prg, Tree *lhs, long position )
{
	Kid *pos = tree_child( prg, lhs );
	while ( position > 0 ) {
		pos = pos->next;
		position -= 1;
	}
	return pos->tree;
}

void set_field( Program *prg, Tree *tree, long field, Tree *value )
{
	assert( tree->refs == 1 );
	if ( value != 0 )
		assert( value->refs >= 1 );
	set_attr( tree, field, value );
}

Tree *get_field( Tree *tree, Word field )
{
	return get_attr( tree, field );
}

Kid *get_field_kid( Tree *tree, Word field )
{
	return get_attr_kid( tree, field );
}

Tree *get_field_split( Program *prg, Tree *tree, Word field )
{
	Tree *val = get_attr( tree, field );
	Tree *split = split_tree( prg, val );
	set_attr( tree, field, split );
	return split;
}

void set_triter_cur( TreeIter *iter, Tree *tree )
{
	iter->ref.kid->tree = tree;
}

Tree *get_ptr_val( Pointer *ptr )
{
	return ptr->value->tree;
}

Tree *get_ptr_val_split( Program *prg, Pointer *ptr )
{
	Tree *val = ptr->value->tree;
	Tree *split = split_tree( prg, val );
	ptr->value->tree = split;
	return split;
}

void iter_find( Program *prg, Tree **&sp, TreeIter *iter, bool tryFirst )
{
	bool anyTree = iter->searchId == prg->rtd->anyId;
	Tree **top = iter->stackRoot;
	Kid *child;

rec_call:
	if ( tryFirst && ( iter->ref.kid->tree->id == iter->searchId || anyTree ) )
		return;
	else {
		child = tree_child( prg, iter->ref.kid->tree );
		if ( child != 0 ) {
			vm_push( (SW) iter->ref.next );
			vm_push( (SW) iter->ref.kid );
			iter->ref.kid = child;
			iter->ref.next = (Ref*)vm_ptop();
			while ( iter->ref.kid != 0 ) {
				tryFirst = true;
				goto rec_call;
				rec_return:
				iter->ref.kid = iter->ref.kid->next;
			}
			iter->ref.kid = (Kid*)vm_pop();
			iter->ref.next = (Ref*)vm_pop();
		}
	}

	if ( top != vm_ptop() )
		goto rec_return;
	
	iter->ref.kid = 0;
}

Tree *tree_iter_advance( Program *prg, Tree **&sp, TreeIter *iter )
{
	assert( iter->stackSize == iter->stackRoot - vm_ptop() );

	if ( iter->ref.kid == 0 ) {
		/* Kid is zero, start from the root. */
		iter->ref = iter->rootRef;
		iter_find( prg, sp, iter, true );
	}
	else {
		/* Have a previous item, continue searching from there. */
		iter_find( prg, sp, iter, false );
	}

	iter->stackSize = iter->stackRoot - vm_ptop();

	return (iter->ref.kid ? prg->trueVal : prg->falseVal );
}

Tree *tree_iter_next_child( Program *prg, Tree **&sp, TreeIter *iter )
{
	assert( iter->stackSize == iter->stackRoot - vm_ptop() );
	Kid *kid = 0;

	if ( iter->ref.kid == 0 ) {
		/* Kid is zero, start from the first child. */
		Kid *child = tree_child( prg, iter->rootRef.kid->tree );

		if ( child == 0 )
			iter->ref.next = 0;
		else {
			/* Make a reference to the root. */
			vm_push( (SW) iter->rootRef.next );
			vm_push( (SW) iter->rootRef.kid );
			iter->ref.next = (Ref*)vm_ptop();

			kid = child;
		}
	}
	else {
		/* Start at next. */
		kid = iter->ref.kid->next;
	}

	if ( iter->searchId != prg->rtd->anyId ) {
		/* Have a previous item, go to the next sibling. */
		while ( kid != 0 && kid->tree->id != iter->searchId )
			kid = kid->next;
	}

	iter->ref.kid = kid;
	iter->stackSize = iter->stackRoot - vm_ptop();

	return ( iter->ref.kid ? prg->trueVal : prg->falseVal );
}

Tree *tree_rev_iter_prev_child( Program *prg, Tree **&sp, RevTreeIter *iter )
{
	assert( iter->stackSize == iter->stackRoot - vm_ptop() );

	if ( iter->kidAtYield != iter->ref.kid ) {
		/* Need to reload the kids. */
		Kid *kid = tree_child( prg, iter->rootRef.kid->tree );
		Kid **dst = (Kid**)iter->stackRoot - 1;
		while ( kid != 0 ) {
			*dst-- = kid;
			kid = kid->next;
		}
	}

	if ( iter->ref.kid == 0 )
		iter->cur = (Kid**)iter->stackRoot - iter->children;
	else
		iter->cur += 1;

	if ( iter->searchId != prg->rtd->anyId ) {
		/* Have a previous item, go to the next sibling. */
		while ( iter->cur != (Kid**)iter->stackRoot && (*iter->cur)->tree->id != iter->searchId )
			iter->cur += 1;
	}

	if ( iter->cur == (Kid**)iter->stackRoot ) {
		iter->ref.next = 0;
		iter->ref.kid = 0;
	}
	else {
		iter->ref.next = &iter->rootRef;
		iter->ref.kid = *iter->cur;
	}

	/* We will use this to detect a split above the iterated tree. */
	iter->kidAtYield = iter->ref.kid;

	iter->stackSize = iter->stackRoot - vm_ptop();

	return (iter->ref.kid ? prg->trueVal : prg->falseVal );
}

void iter_find_repeat( Program *prg, Tree **&sp, TreeIter *iter, bool tryFirst )
{
	bool anyTree = iter->searchId == prg->rtd->anyId;
	Tree **top = iter->stackRoot;
	Kid *child;

rec_call:
	if ( tryFirst && ( iter->ref.kid->tree->id == iter->searchId || anyTree ) )
		return;
	else {
		/* The repeat iterator is just like the normal top-down-left-right,
		 * execept it only goes into the children of a node if the node is the
		 * root of the iteration, or if does not have any neighbours to the
		 * right. */
		if ( top == vm_ptop() || iter->ref.kid->next == 0  ) {
			child = tree_child( prg, iter->ref.kid->tree );
			if ( child != 0 ) {
				vm_push( (SW) iter->ref.next );
				vm_push( (SW) iter->ref.kid );
				iter->ref.kid = child;
				iter->ref.next = (Ref*)vm_ptop();
				while ( iter->ref.kid != 0 ) {
					tryFirst = true;
					goto rec_call;
					rec_return:
					iter->ref.kid = iter->ref.kid->next;
				}
				iter->ref.kid = (Kid*)vm_pop();
				iter->ref.next = (Ref*)vm_pop();
			}
		}
	}

	if ( top != vm_ptop() )
		goto rec_return;
	
	iter->ref.kid = 0;
}

Tree *tree_iter_next_repeat( Program *prg, Tree **&sp, TreeIter *iter )
{
	assert( iter->stackSize == iter->stackRoot - vm_ptop() );

	if ( iter->ref.kid == 0 ) {
		/* Kid is zero, start from the root. */
		iter->ref = iter->rootRef;
		iter_find_repeat( prg, sp, iter, true );
	}
	else {
		/* Have a previous item, continue searching from there. */
		iter_find_repeat( prg, sp, iter, false );
	}

	iter->stackSize = iter->stackRoot - vm_ptop();

	return (iter->ref.kid ? prg->trueVal : prg->falseVal );
}

void iter_find_rev_repeat( Program *prg, Tree **&sp, TreeIter *iter, bool tryFirst )
{
	bool anyTree = iter->searchId == prg->rtd->anyId;
	Tree **top = iter->stackRoot;
	Kid *child;

	if ( tryFirst ) {
		while ( true ) {
			if ( top == vm_ptop() || iter->ref.kid->next == 0 ) {
				child = tree_child( prg, iter->ref.kid->tree );

				if ( child == 0 )
					break;
				vm_push( (SW) iter->ref.next );
				vm_push( (SW) iter->ref.kid );
				iter->ref.kid = child;
				iter->ref.next = (Ref*)vm_ptop();
			}
			else {
				/* Not the top and not there is a next, go over to it. */
				iter->ref.kid = iter->ref.kid->next;
			}
		}

		goto first;
	}

	while ( true ) {
		if ( top == vm_ptop() ) {
			iter->ref.kid = 0;
			return;
		}
		
		if ( iter->ref.kid->next == 0 ) {
			/* Go up one and then down. Remember we can't use iter->ref.next
			 * because the chain may have been split, setting it null (to
			 * prevent repeated walks up). */
			Ref *ref = (Ref*)vm_ptop();
			iter->ref.kid = ref->kid->tree->child;
		}
		else {
			iter->ref.kid = (Kid*)vm_pop();
			iter->ref.next = (Ref*)vm_pop();
		}
first:
		if ( iter->ref.kid->tree->id == iter->searchId || anyTree )
			return;
	}

	return;
}

Tree *tree_iter_prev_repeat( Program *prg, Tree **&sp, TreeIter *iter )
{
	assert( iter->stackSize == iter->stackRoot - vm_ptop() );

	if ( iter->ref.kid == 0 ) {
		/* Kid is zero, start from the root. */
		iter->ref = iter->rootRef;
		iter_find_rev_repeat( prg, sp, iter, true );
	}
	else {
		/* Have a previous item, continue searching from there. */
		iter_find_rev_repeat( prg, sp, iter, false );
	}

	iter->stackSize = iter->stackRoot - vm_ptop();

	return (iter->ref.kid ? prg->trueVal : prg->falseVal );
}

Tree *tree_search( Program *prg, Kid *kid, long id )
{
	/* This node the one? */
	if ( kid->tree->id == id )
		return kid->tree;

	Tree *res = 0;

	/* Search children. */
	Kid *child = tree_child( prg, kid->tree );
	if ( child != 0 )
		res = tree_search( prg, child, id );
	
	/* Search siblings. */
	if ( res == 0 && kid->next != 0 )
		res = tree_search( prg, kid->next, id );

	return res;	
}

Tree *tree_search( Program *prg, Tree *tree, long id )
{
	Tree *res = 0;
	if ( tree->id == id )
		res = tree;
	else {
		Kid *child = tree_child( prg, tree );
		if ( child != 0 )
			res = tree_search( prg, child, id );
	}
	return res;
}

bool map_insert( Program *prg, Map *map, Tree *key, Tree *element )
{
	MapEl *mapEl = map->insert( prg, key );

	if ( mapEl != 0 ) {
		mapEl->tree = element;
		return true;
	}

	return false;
}

void map_unremove( Program *prg, Map *map, Tree *key, Tree *element )
{
	MapEl *mapEl = map->insert( prg, key );
	assert( mapEl != 0 );
	mapEl->tree = element;
}

Tree *map_uninsert( Program *prg, Map *map, Tree *key )
{
	MapEl *el = map->detach( prg, key );
	Tree *val = el->tree;
	prg->mapElPool.free( el );
	return val;
}

Tree *map_store( Program *prg, Map *map, Tree *key, Tree *element )
{
	Tree *oldTree = 0;
	MapEl *elInTree = 0;
	MapEl *mapEl = map->insert( prg, key, &elInTree );

	if ( mapEl != 0 )
		mapEl->tree = element;
	else {
		/* Element with key exists. Overwriting the value. */
		oldTree = elInTree->tree;
		elInTree->tree = element;
	}

	return oldTree;
}

Tree *map_unstore( Program *prg, Map *map, Tree *key, Tree *existing )
{
	Tree *stored = 0;
	if ( existing == 0 ) {
		MapEl *mapEl = map->detach( prg, key );
		stored = mapEl->tree;
		prg->mapElPool.free( mapEl );
	}
	else {
		MapEl *mapEl = map->find( prg, key );
		stored = mapEl->tree;
		mapEl->tree = existing;
	}
	return stored;
}

Tree *map_find( Program *prg, Map *map, Tree *key )
{
	MapEl *mapEl = map->find( prg, key );
	return mapEl == 0 ? 0 : mapEl->tree;
}

long map_length( Map *map )
{
	return map->length();
}

long list_length( List *list )
{
	return list->length();
}

void list_append( Program *prg, List *list, Tree *val )
{
	assert( list->refs == 1 );
	if ( val != 0 )
		assert( val->refs >= 1 );
	ListEl *listEl = prg->listElPool.allocate();
	listEl->value = val;
	list->append( listEl );
}

Tree *list_remove_end( Program *prg, List *list )
{
	Tree *tree = list->tail->value;
	prg->listElPool.free( list->detachLast() );
	return tree;
}

Tree *get_list_mem( List *list, Word field )
{
	Tree *result = 0;
	switch ( field ) {
		case 0: 
			result = list->head->value;
			break;
		case 1: 
			result = list->tail->value;
			break;
		default:
			assert( false );
			break;
	}
	return result;
}

Tree *get_list_mem_split( Program *prg, List *list, Word field )
{
	Tree *sv = 0;
	switch ( field ) {
		case 0: 
			sv = split_tree( prg, list->head->value );
			list->head->value = sv; 
			break;
		case 1: 
			sv = split_tree( prg, list->tail->value );
			list->tail->value = sv; 
			break;
		default:
			assert( false );
			break;
	}
	return sv;
}

Tree *set_list_mem( List *list, Half field, Tree *value )
{
	assert( list->refs == 1 );
	if ( value != 0 )
		assert( value->refs >= 1 );

	Tree *existing = 0;
	switch ( field ) {
		case 0:
			existing = list->head->value;
			list->head->value = value;
			break;
		case 1:
			existing = list->tail->value;
			list->tail->value = value;
			break;
		default:
			assert( false );
			break;
	}
	return existing;
}

TreePair map_remove( Program *prg, Map *map, Tree *key )
{
	MapEl *mapEl = map->find( prg, key );
	TreePair result;
	if ( mapEl != 0 ) {
		map->detach( prg, mapEl );
		result.key = mapEl->key;
		result.val = mapEl->tree;
		prg->mapElPool.free( mapEl );
	}

	return result;
}

void split_ref( Tree **&sp, Program *prg, Ref *fromRef )
{
	/* Go up the chain of kids, turing the pointers down. */
	Ref *last = 0, *ref = fromRef, *next = 0;
	while ( ref->next != 0 ) {
		next = ref->next;
		ref->next = last;
		last = ref;
		ref = next;
	}
	ref->next = last;

	/* Now traverse the list, which goes down. */
	while ( ref != 0 ) {
		if ( ref->kid->tree->refs > 1 ) {
			#ifdef COLM_LOG_BYTECODE
			if ( colm_log_bytecode ) {
				cerr << "splitting tree: " << ref->kid << " refs: " << 
						ref->kid->tree->refs << endl;
			}
			#endif

			Ref *nextDown = ref->next;
			while ( nextDown != 0 && nextDown->kid == ref->kid )
				nextDown = nextDown->next;

			Kid *oldNextKidDown = nextDown != 0 ? nextDown->kid : 0;
			Kid *newNextKidDown = 0;

			Tree *newTree = copy_tree( prg, ref->kid->tree, 
					oldNextKidDown, newNextKidDown );
			tree_upref( newTree );
			
			/* Downref the original. Don't need to consider freeing because
			 * refs were > 1. */
			ref->kid->tree->refs -= 1;

			while ( ref != 0 && ref != nextDown ) {
				next = ref->next;
				ref->next = 0;

				ref->kid->tree = newTree;
				ref = next;
			}

			/* Correct kid pointers down from ref. */
			while ( nextDown != 0 && nextDown->kid == oldNextKidDown ) {
				nextDown->kid = newNextKidDown;
				nextDown = nextDown->next;
			}
		}
		else {
			/* Reset the list as we go down. */
			next = ref->next;
			ref->next = 0;
			ref = next;
		}
	}
}

void split_iter_cur( Tree **&sp, Program *prg, TreeIter *iter )
{
	if ( iter->ref.kid == 0 )
		return;
	
	split_ref( sp, prg, &iter->ref );
}

long cmp_tree( Program *prg, const Tree *tree1, const Tree *tree2 )
{
	long cmpres = 0;
	if ( tree1 == 0 ) {
		if ( tree2 == 0 )
			return 0;
		else
			return -1;
	}
	else if ( tree2 == 0 )
		return 1;
	else if ( tree1->id < tree2->id )
		return -1;
	else if ( tree1->id > tree2->id )
		return 1;
	else if ( tree1->id == LEL_ID_PTR ) {
		if ( ((Pointer*)tree1)->value < ((Pointer*)tree2)->value )
			return -1;
		else if ( ((Pointer*)tree1)->value > ((Pointer*)tree2)->value )
			return 1;
	}
	else if ( tree1->id == LEL_ID_INT ) {
		if ( ((Int*)tree1)->value < ((Int*)tree2)->value )
			return -1;
		else if ( ((Int*)tree1)->value > ((Int*)tree2)->value )
			return 1;
	}
	else if ( tree1->id == LEL_ID_STR ) {
		cmpres = cmp_string( ((Str*)tree1)->value, ((Str*)tree2)->value );
		if ( cmpres != 0 )
			return cmpres;
	}
	else {
		if ( tree1->tokdata == 0 && tree2->tokdata != 0 )
			return -1;
		else if ( tree1->tokdata != 0 && tree2->tokdata == 0 )
			return 1;
		else if ( tree1->tokdata != 0 && tree2->tokdata != 0 ) {
			cmpres = cmp_string( tree1->tokdata, tree2->tokdata );
			if ( cmpres != 0 )
				return cmpres;
		}
	}

	Kid *kid1 = tree_child( prg, tree1 );
	Kid *kid2 = tree_child( prg, tree2 );

	while ( true ) {
		if ( kid1 == 0 && kid2 == 0 )
			return 0;
		else if ( kid1 == 0 && kid2 != 0 )
			return -1;
		else if ( kid1 != 0 && kid2 == 0 )
			return 1;
		else {
			cmpres = cmp_tree( prg, kid1->tree, kid2->tree );
			if ( cmpres != 0 )
				return cmpres;
		}
		kid1 = kid1->next;
		kid2 = kid2->next;
	}
}

/* This must traverse in the same order that the bindId assignments are done
 * in. */
bool match_pattern( Tree **bindings, Program *prg, long pat, Kid *kid, bool checkNext )
{
	PatReplNode *nodes = prg->rtd->patReplNodes;

	#ifdef COLM_LOG_MATCH
	if ( colm_log_match ) {
		LangElInfo *lelInfo = prg->rtd->lelInfo;
		cerr << "match_pattern " << ( pat == -1 ? "NULL" : lelInfo[nodes[pat].id].name ) <<
				" vs " << ( kid == 0 ? "NULL" : lelInfo[kid->tree->id].name ) << endl;
	}
	#endif

	/* match node, recurse on children. */
	if ( pat != -1 && kid != 0 ) {
		if ( nodes[pat].id == kid->tree->id ) {
			/* If the pattern node has data, then this means we need to match
			 * the data against the token data. */
			if ( nodes[pat].data != 0 ) {
				/* Check the length of token text. */
				if ( nodes[pat].length != string_length( kid->tree->tokdata ) )
					return false;

				/* Check the token text data. */
				if ( nodes[pat].length > 0 && memcmp( nodes[pat].data, 
						string_data( kid->tree->tokdata ), nodes[pat].length ) != 0 )
					return false;
			}

			/* No failure, all okay. */
			if ( nodes[pat].bindId > 0 ) {
				#ifdef COLM_LOG_MATCH
				if ( colm_log_match ) {
					cerr << "bindId: " << nodes[pat].bindId << endl;
				}
				#endif
				bindings[nodes[pat].bindId] = kid->tree;
			}

			/* If we didn't match a terminal duplicate of a nonterm then check
			 * down the children. */
			if ( !nodes[pat].stop ) {
				/* Check for failure down child branch. */
				bool childCheck = match_pattern( bindings, prg, 
						nodes[pat].child, tree_child( prg, kid->tree ), true );
				if ( ! childCheck )
					return false;
			}

			/* If checking next, then look for failure there. */
			if ( checkNext ) {
				bool nextCheck = match_pattern( bindings, prg, 
						nodes[pat].next, kid->next, true );
				if ( ! nextCheck )
					return false;
			}

			return true;
		}
	}
	else if ( pat == -1 && kid == 0 ) {
		/* Both null is a match. */
		return 1;
	}

	return false;
}

