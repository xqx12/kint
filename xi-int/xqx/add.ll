; ModuleID = 'add.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @adds32(i32 %x, i32 %y) nounwind uwtable {
entry:
  call void @trap.sadd.i32(i32 %x, i32 %y), !dbg !16
  %add = add nsw i32 %x, %y, !dbg !16
  ret i32 %add, !dbg !16
}

define i64 @addu64(i64 %x, i64 %y) nounwind uwtable {
entry:
  call void @trap.uadd.i64(i64 %x, i64 %y), !dbg !18
  %add = add i64 %x, %y, !dbg !18
  ret i64 %add, !dbg !18
}

declare void @trap.sadd.i32(i32, i32) nounwind

declare void @trap.uadd.i64(i64, i64) nounwind

!llvm.dbg.cu = !{!0}
!cint.structs = !{}

!0 = metadata !{i32 786449, i32 0, i32 12, metadata !"add.c", metadata !"/home/xqx/kint/xi-int/xqx", metadata !"clang version 3.1 (tags/RELEASE_31/final 176548)", i1 true, i1 false, metadata !"", i32 0, metadata !1, metadata !1, metadata !3, metadata !1} ; [ DW_TAG_compile_unit ]
!1 = metadata !{metadata !2}
!2 = metadata !{i32 0}
!3 = metadata !{metadata !4}
!4 = metadata !{metadata !5, metadata !12}
!5 = metadata !{i32 786478, i32 0, metadata !6, metadata !"adds32", metadata !"adds32", metadata !"", metadata !6, i32 1, metadata !7, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, i32 (i32, i32)* @adds32, null, null, metadata !10, i32 2} ; [ DW_TAG_subprogram ]
!6 = metadata !{i32 786473, metadata !"add.c", metadata !"/home/xqx/kint/xi-int/xqx", null} ; [ DW_TAG_file_type ]
!7 = metadata !{i32 786453, i32 0, metadata !"", i32 0, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !8, i32 0, i32 0} ; [ DW_TAG_subroutine_type ]
!8 = metadata !{metadata !9, metadata !9, metadata !9}
!9 = metadata !{i32 786468, null, metadata !"int", null, i32 0, i64 32, i64 32, i64 0, i32 0, i32 5} ; [ DW_TAG_base_type ]
!10 = metadata !{metadata !11}
!11 = metadata !{i32 786468}                      ; [ DW_TAG_base_type ]
!12 = metadata !{i32 786478, i32 0, metadata !6, metadata !"addu64", metadata !"addu64", metadata !"", metadata !6, i32 8, metadata !13, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, i64 (i64, i64)* @addu64, null, null, metadata !10, i32 9} ; [ DW_TAG_subprogram ]
!13 = metadata !{i32 786453, i32 0, metadata !"", i32 0, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !14, i32 0, i32 0} ; [ DW_TAG_subroutine_type ]
!14 = metadata !{metadata !15, metadata !15, metadata !15}
!15 = metadata !{i32 786468, null, metadata !"long long unsigned int", null, i32 0, i64 64, i64 64, i64 0, i32 0, i32 7} ; [ DW_TAG_base_type ]
!16 = metadata !{i32 5, i32 2, metadata !17, null}
!17 = metadata !{i32 786443, metadata !5, i32 2, i32 1, metadata !6, i32 0} ; [ DW_TAG_lexical_block ]
!18 = metadata !{i32 12, i32 2, metadata !19, null}
!19 = metadata !{i32 786443, metadata !12, i32 9, i32 1, metadata !6, i32 1} ; [ DW_TAG_lexical_block ]
