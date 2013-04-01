; ModuleID = 's2etest.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [7 x i8] c"n = 0\0A\00", align 1
@.str1 = private unnamed_addr constant [10 x i8] c"n*2 <= 1\0A\00", align 1
@.str2 = private unnamed_addr constant [10 x i8] c"n*2 <= 4\0A\00", align 1
@.str3 = private unnamed_addr constant [10 x i8] c"n*2 >= 1\0A\00", align 1

define i32 @mtest(i32 %nbuf) nounwind uwtable {
entry:
  %nbuf.addr = alloca i32, align 4
  %buf = alloca [5 x i8], align 1
  store i32 %nbuf, i32* %nbuf.addr, align 4
  %cmp = icmp eq i32 %nbuf, 0, !dbg !21
  br i1 %cmp, label %if.then.split, label %if.else.split, !dbg !21

if.then.split:                                    ; preds = %entry
  %call = call i32 (...)* @cprintf(i8* getelementptr inbounds ([7 x i8]* @.str, i32 0, i32 0)), !dbg !23
  br label %if.end12, !dbg !25

if.else.split:                                    ; preds = %entry
  call void @trap.umul.i32(i32 %nbuf, i32 2), !dbg !26
  %mul = mul i32 %nbuf, 2, !dbg !26
  %cmp1 = icmp eq i32 %mul, 11, !dbg !26
  br i1 %cmp1, label %if.then2.split, label %if.else4, !dbg !26

if.then2.split:                                   ; preds = %if.else.split
  %call3 = call i32 (...)* @cprintf(i8* getelementptr inbounds ([10 x i8]* @.str1, i32 0, i32 0)), !dbg !28
  br label %if.end12, !dbg !30

if.else4:                                         ; preds = %if.else.split
  %cmp6 = icmp ule i32 %mul, 2, !dbg !31
  br i1 %cmp6, label %if.then7.split, label %if.else9.split, !dbg !31

if.then7.split:                                   ; preds = %if.else4
  %call8 = call i32 (...)* @cprintf(i8* getelementptr inbounds ([10 x i8]* @.str2, i32 0, i32 0)), !dbg !32
  br label %if.end12, !dbg !34

if.else9.split:                                   ; preds = %if.else4
  %call10 = call i32 (...)* @cprintf(i8* getelementptr inbounds ([10 x i8]* @.str3, i32 0, i32 0)), !dbg !35
  br label %if.end12

if.end12:                                         ; preds = %if.else9.split, %if.then7.split, %if.then2.split, %if.then.split
  %arraydecay = getelementptr inbounds [5 x i8]* %buf, i32 0, i32 0, !dbg !37
  call void @llvm.memset.p0i8.i64(i8* %arraydecay, i8 0, i64 5, i32 1, i1 false), !dbg !37
  %tmp = bitcast i32* %nbuf.addr to i8*, !dbg !38
  %call14 = call i8* @strcpy(i8* %arraydecay, i8* %tmp) nounwind, !dbg !38
  ret i32 0, !dbg !39
}

declare i32 @cprintf(...)

declare void @llvm.memset.p0i8.i64(i8* nocapture, i8, i64, i32, i1) nounwind

declare i8* @strcpy(i8*, i8*) nounwind

declare void @trap.umul.i32(i32, i32) nounwind

!llvm.dbg.cu = !{!0}
!cint.structs = !{!13, !14, !15, !16, !17, !18, !19, !20}

!0 = metadata !{i32 786449, i32 0, i32 12, metadata !"s2etest.c", metadata !"/home/xqx/kint/xi-int/xqx", metadata !"clang version 3.1 (tags/RELEASE_31/final 176548)", i1 true, i1 false, metadata !"", i32 0, metadata !1, metadata !1, metadata !3, metadata !1} ; [ DW_TAG_compile_unit ]
!1 = metadata !{metadata !2}
!2 = metadata !{i32 0}
!3 = metadata !{metadata !4}
!4 = metadata !{metadata !5}
!5 = metadata !{i32 786478, i32 0, metadata !6, metadata !"mtest", metadata !"mtest", metadata !"", metadata !6, i32 5, metadata !7, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, i32 (i32)* @mtest, null, null, metadata !11, i32 6} ; [ DW_TAG_subprogram ]
!6 = metadata !{i32 786473, metadata !"s2etest.c", metadata !"/home/xqx/kint/xi-int/xqx", null} ; [ DW_TAG_file_type ]
!7 = metadata !{i32 786453, i32 0, metadata !"", i32 0, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !8, i32 0, i32 0} ; [ DW_TAG_subroutine_type ]
!8 = metadata !{metadata !9, metadata !10}
!9 = metadata !{i32 786468, null, metadata !"int", null, i32 0, i64 32, i64 32, i64 0, i32 0, i32 5} ; [ DW_TAG_base_type ]
!10 = metadata !{i32 786468, null, metadata !"unsigned int", null, i32 0, i64 32, i64 32, i64 0, i32 0, i32 7} ; [ DW_TAG_base_type ]
!11 = metadata !{metadata !12}
!12 = metadata !{i32 786468}                      ; [ DW_TAG_base_type ]
!13 = metadata !{metadata !"__fsid_t", metadata !"__val"}
!14 = metadata !{metadata !"anon.wchar.h.86", metadata !"__wch.UInt", metadata !"__wchb"}
!15 = metadata !{metadata !"__mbstate_t", metadata !"__count.Int", metadata !"__value"}
!16 = metadata !{metadata !"_G_fpos_t", metadata !"__pos.Long", metadata !"__state"}
!17 = metadata !{metadata !"_G_fpos64_t", metadata !"__pos.Long", metadata !"__state"}
!18 = metadata !{metadata !"_IO_marker", metadata !"_next", metadata !"_sbuf", metadata !"_pos.Int"}
!19 = metadata !{metadata !"_IO_FILE", metadata !"_flags.Int", metadata !"_IO_read_ptr", metadata !"_IO_read_end", metadata !"_IO_read_base", metadata !"_IO_write_base", metadata !"_IO_write_ptr", metadata !"_IO_write_end", metadata !"_IO_buf_base", metadata !"_IO_buf_end", metadata !"_IO_save_base", metadata !"_IO_backup_base", metadata !"_IO_save_end", metadata !"_markers", metadata !"_chain", metadata !"_fileno.Int", metadata !"_flags2.Int", metadata !"_old_offset.Long", metadata !"_cur_column.UShort", metadata !"_vtable_offset.SChar", metadata !"_shortbuf", metadata !"_lock", metadata !"_offset.Long", metadata !"__pad1", metadata !"__pad2", metadata !"__pad3", metadata !"__pad4", metadata !"__pad5.ULong", metadata !"_mode.Int", metadata !"_unused2"}
!20 = metadata !{metadata !"__locale_struct", metadata !"__locales", metadata !"__ctype_b", metadata !"__ctype_tolower", metadata !"__ctype_toupper", metadata !"__names"}
!21 = metadata !{i32 21, i32 4, metadata !22, null}
!22 = metadata !{i32 786443, metadata !5, i32 6, i32 1, metadata !6, i32 0} ; [ DW_TAG_lexical_block ]
!23 = metadata !{i32 22, i32 7, metadata !24, null}
!24 = metadata !{i32 786443, metadata !22, i32 21, i32 17, metadata !6, i32 1} ; [ DW_TAG_lexical_block ]
!25 = metadata !{i32 23, i32 5, metadata !24, null}
!26 = metadata !{i32 25, i32 2, metadata !27, null}
!27 = metadata !{i32 786443, metadata !22, i32 24, i32 8, metadata !6, i32 2} ; [ DW_TAG_lexical_block ]
!28 = metadata !{i32 26, i32 11, metadata !29, null}
!29 = metadata !{i32 786443, metadata !27, i32 25, i32 18, metadata !6, i32 3} ; [ DW_TAG_lexical_block ]
!30 = metadata !{i32 27, i32 9, metadata !29, null}
!31 = metadata !{i32 28, i32 12, metadata !27, null}
!32 = metadata !{i32 29, i32 11, metadata !33, null}
!33 = metadata !{i32 786443, metadata !27, i32 28, i32 27, metadata !6, i32 4} ; [ DW_TAG_lexical_block ]
!34 = metadata !{i32 30, i32 9, metadata !33, null}
!35 = metadata !{i32 32, i32 11, metadata !36, null}
!36 = metadata !{i32 786443, metadata !27, i32 31, i32 11, metadata !6, i32 5} ; [ DW_TAG_lexical_block ]
!37 = metadata !{i32 75, i32 3, metadata !22, null}
!38 = metadata !{i32 76, i32 3, metadata !22, null}
!39 = metadata !{i32 83, i32 3, metadata !22, null}
