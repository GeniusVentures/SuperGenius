/**
 * @file       bytecode.hpp
 * @brief      
 * @date       2024-10-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _BYTECODE_TEST_HPP_
#define _BYTECODE_TEST_HPP_

static constexpr std::string_view ByteCodeTest = R"BYTECODE(
; ModuleID = 'llvm-link'
source_filename = "llvm-link"
target datalayout = "e-m:e-p:64:8-a:8-i16:8-i32:8-i64:8-v768:8-v1152:8-v1536:8"
target triple = "assigner"

$__clang_call_terminate = comdat any

$_ZN3nil7crypto37algebra6fields17pallas_base_field12modulus_bitsE = comdat any

$_ZN3nil7crypto37algebra6fields17pallas_base_field11number_bitsE = comdat any

$_ZN3nil7crypto37algebra6fields17pallas_base_field10value_bitsE = comdat any

$_ZN3nil7crypto37algebra6fields16vesta_base_field12modulus_bitsE = comdat any

$_ZN3nil7crypto37algebra6fields16vesta_base_field11number_bitsE = comdat any

$_ZN3nil7crypto37algebra6fields16vesta_base_field10value_bitsE = comdat any

@_ZN3nil7crypto37algebra6fields17pallas_base_field12modulus_bitsE = weak_odr dso_local local_unnamed_addr constant i64 255, comdat, align 8
@_ZN3nil7crypto37algebra6fields17pallas_base_field11number_bitsE = weak_odr dso_local local_unnamed_addr constant i64 255, comdat, align 8
@_ZN3nil7crypto37algebra6fields17pallas_base_field10value_bitsE = weak_odr dso_local local_unnamed_addr constant i64 255, comdat, align 8
@_ZN3nil7crypto37algebra6fields16vesta_base_field12modulus_bitsE = weak_odr dso_local local_unnamed_addr constant i64 255, comdat, align 8
@_ZN3nil7crypto37algebra6fields16vesta_base_field11number_bitsE = weak_odr dso_local local_unnamed_addr constant i64 255, comdat, align 8
@_ZN3nil7crypto37algebra6fields16vesta_base_field10value_bitsE = weak_odr dso_local local_unnamed_addr constant i64 255, comdat, align 8

; Function Attrs: mustprogress nounwind
define dso_local noundef __zkllvm_field_pallas_base @_Z5pow_2u26__zkllvm_field_pallas_base(__zkllvm_field_pallas_base noundef %0) local_unnamed_addr #0 {
  %2 = mul __zkllvm_field_pallas_base f0x1, %0
  %3 = mul __zkllvm_field_pallas_base %2, %0
  ret __zkllvm_field_pallas_base %3
}

; Function Attrs: circuit mustprogress nounwind
define dso_local noundef __zkllvm_field_pallas_base @_Z24field_arithmetic_exampleu26__zkllvm_field_pallas_baseu26__zkllvm_field_pallas_base(__zkllvm_field_pallas_base noundef %0, __zkllvm_field_pallas_base noundef %1) local_unnamed_addr #1 {
  %3 = add __zkllvm_field_pallas_base %0, %1
  %4 = mul __zkllvm_field_pallas_base %3, %0
  %5 = mul __zkllvm_field_pallas_base %1, %3
  %6 = mul __zkllvm_field_pallas_base %5, %3
  %7 = add __zkllvm_field_pallas_base %4, %6
  %8 = mul __zkllvm_field_pallas_base %7, %7
  %9 = mul __zkllvm_field_pallas_base %8, %7
  %10 = sub __zkllvm_field_pallas_base %1, %0
  %11 = sdiv __zkllvm_field_pallas_base %9, %10
  %12 = tail call noundef __zkllvm_field_pallas_base @_Z5pow_2u26__zkllvm_field_pallas_base(__zkllvm_field_pallas_base noundef %0)
  %13 = add __zkllvm_field_pallas_base %11, %12
  %14 = add __zkllvm_field_pallas_base %13, f0x12345678901234567890
  ret __zkllvm_field_pallas_base %14
}

; Function Attrs: nounwind
define dso_local void @free(ptr noundef %0) local_unnamed_addr #2 {
  tail call void @llvm.assigner.free(ptr %0)
  ret void
}

; Function Attrs: nounwind
declare void @llvm.assigner.free(ptr) #3

; Function Attrs: nounwind allocsize(0)
define dso_local ptr @malloc(i64 noundef %0) local_unnamed_addr #4 {
  %2 = tail call ptr @llvm.assigner.malloc(i64 %0)
  ret ptr %2
}

; Function Attrs: nounwind
declare ptr @llvm.assigner.malloc(i64) #3

; Function Attrs: nounwind
define dso_local i64 @strlen(ptr noundef %0) local_unnamed_addr #2 {
  br label %2

2:                                                ; preds = %2, %1
  %3 = phi i64 [ 0, %1 ], [ %8, %2 ]
  %4 = getelementptr inbounds i8, ptr %0, i64 %3
  %5 = load i8, ptr %4, align 1, !tbaa !3
  %6 = sext i8 %5 to i32
  %7 = icmp ne i32 %6, 0
  %8 = add i64 %3, 1
  br i1 %7, label %2, label %9, !llvm.loop !6

9:                                                ; preds = %2
  ret i64 %3
}

; Function Attrs: nounwind
define dso_local i32 @strcmp(ptr noundef %0, ptr noundef %1) local_unnamed_addr #2 {
  br label %3

3:                                                ; preds = %21, %2
  %4 = phi ptr [ %0, %2 ], [ %22, %21 ]
  %5 = phi ptr [ %1, %2 ], [ %23, %21 ]
  %6 = load i8, ptr %4, align 1, !tbaa !3
  %7 = sext i8 %6 to i32
  %8 = icmp ne i32 %7, 0
  br i1 %8, label %13, label %9

9:                                                ; preds = %3
  %10 = load i8, ptr %5, align 1, !tbaa !3
  %11 = sext i8 %10 to i32
  %12 = icmp ne i32 %11, 0
  br i1 %12, label %13, label %24

13:                                               ; preds = %9, %3
  %14 = load i8, ptr %4, align 1, !tbaa !3
  %15 = sext i8 %14 to i32
  %16 = load i8, ptr %5, align 1, !tbaa !3
  %17 = sext i8 %16 to i32
  %18 = icmp ne i32 %15, %17
  br i1 %18, label %19, label %21

19:                                               ; preds = %13
  %20 = sub nsw i32 %15, %17
  br label %24

21:                                               ; preds = %13
  %22 = getelementptr inbounds i8, ptr %4, i32 1
  %23 = getelementptr inbounds i8, ptr %5, i32 1
  br label %3, !llvm.loop !9

24:                                               ; preds = %19, %9
  %25 = phi i32 [ %20, %19 ], [ 0, %9 ]
  ret i32 %25
}

; Function Attrs: nounwind
define dso_local i32 @strncmp(ptr noundef %0, ptr noundef %1, i64 noundef %2) local_unnamed_addr #2 {
  %4 = icmp ugt i64 %2, 0
  br i1 %4, label %5, label %29

5:                                                ; preds = %24, %3
  %6 = phi ptr [ %26, %24 ], [ %1, %3 ]
  %7 = phi ptr [ %25, %24 ], [ %0, %3 ]
  %8 = phi i64 [ %27, %24 ], [ %2, %3 ]
  %9 = load i8, ptr %7, align 1, !tbaa !3
  %10 = sext i8 %9 to i32
  %11 = icmp ne i32 %10, 0
  br i1 %11, label %16, label %12

12:                                               ; preds = %5
  %13 = load i8, ptr %6, align 1, !tbaa !3
  %14 = sext i8 %13 to i32
  %15 = icmp ne i32 %14, 0
  br i1 %15, label %16, label %29

16:                                               ; preds = %12, %5
  %17 = load i8, ptr %7, align 1, !tbaa !3
  %18 = sext i8 %17 to i32
  %19 = load i8, ptr %6, align 1, !tbaa !3
  %20 = sext i8 %19 to i32
  %21 = icmp ne i32 %18, %20
  br i1 %21, label %22, label %24

22:                                               ; preds = %16
  %23 = sub nsw i32 %18, %20
  br label %29

24:                                               ; preds = %16
  %25 = getelementptr inbounds i8, ptr %7, i32 1
  %26 = getelementptr inbounds i8, ptr %6, i32 1
  %27 = add i64 %8, -1
  %28 = icmp ugt i64 %27, 0
  br i1 %28, label %5, label %29, !llvm.loop !10

29:                                               ; preds = %24, %22, %12, %3
  %30 = phi i32 [ %23, %22 ], [ 0, %3 ], [ 0, %12 ], [ 0, %24 ]
  ret i32 %30
}

; Function Attrs: nounwind
define dso_local ptr @strcpy(ptr noundef %0, ptr noundef %1) local_unnamed_addr #2 {
  %3 = load i8, ptr %1, align 1, !tbaa !3
  store i8 %3, ptr %0, align 1, !tbaa !3
  %4 = sext i8 %3 to i32
  %5 = icmp ne i32 %4, 0
  br i1 %5, label %6, label %14

6:                                                ; preds = %6, %2
  %7 = phi ptr [ %10, %6 ], [ %1, %2 ]
  %8 = phi ptr [ %9, %6 ], [ %0, %2 ]
  %9 = getelementptr inbounds i8, ptr %8, i32 1
  %10 = getelementptr inbounds i8, ptr %7, i32 1
  %11 = load i8, ptr %10, align 1, !tbaa !3
  store i8 %11, ptr %9, align 1, !tbaa !3
  %12 = sext i8 %11 to i32
  %13 = icmp ne i32 %12, 0
  br i1 %13, label %6, label %14, !llvm.loop !11

14:                                               ; preds = %6, %2
  ret ptr %0
}

; Function Attrs: nounwind
define dso_local ptr @strncpy(ptr noundef %0, ptr noundef %1, i64 noundef %2) local_unnamed_addr #2 {
  %4 = icmp ugt i64 %2, 0
  br i1 %4, label %5, label %17

5:                                                ; preds = %12, %3
  %6 = phi ptr [ %14, %12 ], [ %1, %3 ]
  %7 = phi ptr [ %13, %12 ], [ %0, %3 ]
  %8 = phi i64 [ %15, %12 ], [ %2, %3 ]
  %9 = load i8, ptr %6, align 1, !tbaa !3
  store i8 %9, ptr %7, align 1, !tbaa !3
  %10 = sext i8 %9 to i32
  %11 = icmp ne i32 %10, 0
  br i1 %11, label %12, label %17

12:                                               ; preds = %5
  %13 = getelementptr inbounds i8, ptr %7, i32 1
  %14 = getelementptr inbounds i8, ptr %6, i32 1
  %15 = add i64 %8, -1
  %16 = icmp ugt i64 %15, 0
  br i1 %16, label %5, label %17, !llvm.loop !12

17:                                               ; preds = %12, %5, %3
  %18 = phi i64 [ %2, %3 ], [ %8, %5 ], [ %15, %12 ]
  %19 = phi ptr [ %0, %3 ], [ %7, %5 ], [ %13, %12 ]
  %20 = icmp ugt i64 %18, 0
  br i1 %20, label %21, label %27

21:                                               ; preds = %21, %17
  %22 = phi ptr [ %24, %21 ], [ %19, %17 ]
  %23 = phi i64 [ %25, %21 ], [ %18, %17 ]
  store i8 0, ptr %22, align 1, !tbaa !3
  %24 = getelementptr inbounds i8, ptr %22, i32 1
  %25 = add i64 %23, -1
  %26 = icmp ugt i64 %25, 0
  br i1 %26, label %21, label %27, !llvm.loop !13

27:                                               ; preds = %21, %17
  ret ptr %0
}

; Function Attrs: mustprogress nobuiltin allocsize(0)
define weak dso_local noundef nonnull ptr @_Znwm(i64 noundef %0) local_unnamed_addr #5 {
  %2 = icmp eq i64 %0, 0
  %3 = select i1 %2, i64 1, i64 %0
  %4 = tail call ptr @llvm.assigner.malloc(i64 %3)
  ret ptr %4
}

; Function Attrs: mustprogress nobuiltin nounwind allocsize(0)
define weak dso_local noalias noundef ptr @_ZnwmRKSt9nothrow_t(i64 noundef %0, ptr noundef nonnull align 1 dereferenceable(1) %1) local_unnamed_addr #6 personality ptr @__gxx_personality_v0 {
  %3 = invoke noalias noundef nonnull ptr @_Znwm(i64 noundef %0) #9
          to label %4 unwind label %5

4:                                                ; preds = %2
  ret ptr %3

5:                                                ; preds = %2
  %6 = landingpad { ptr, i32 }
          catch ptr null
  %7 = extractvalue { ptr, i32 } %6, 0
  tail call void @__clang_call_terminate(ptr %7) #10
  unreachable
}

declare dso_local i32 @__gxx_personality_v0(...)

; Function Attrs: noinline noreturn nounwind
define linkonce_odr hidden void @__clang_call_terminate(ptr noundef %0) local_unnamed_addr #7 comdat {
  %2 = tail call ptr @__cxa_begin_catch(ptr %0) #3
  tail call void @_ZSt9terminatev() #10
  unreachable
}

declare dso_local ptr @__cxa_begin_catch(ptr) local_unnamed_addr

declare dso_local void @_ZSt9terminatev() local_unnamed_addr

; Function Attrs: mustprogress nobuiltin allocsize(0)
define weak dso_local noundef nonnull ptr @_Znam(i64 noundef %0) local_unnamed_addr #5 {
  %2 = tail call noalias noundef nonnull ptr @_Znwm(i64 noundef %0) #9
  ret ptr %2
}

; Function Attrs: mustprogress nobuiltin nounwind allocsize(0)
define weak dso_local noalias noundef ptr @_ZnamRKSt9nothrow_t(i64 noundef %0, ptr noundef nonnull align 1 dereferenceable(1) %1) local_unnamed_addr #6 personality ptr @__gxx_personality_v0 {
  %3 = invoke noalias noundef nonnull ptr @_Znwm(i64 noundef %0) #9
          to label %4 unwind label %5

4:                                                ; preds = %2
  ret ptr %3

5:                                                ; preds = %2
  %6 = landingpad { ptr, i32 }
          catch ptr null
  %7 = extractvalue { ptr, i32 } %6, 0
  tail call void @__clang_call_terminate(ptr %7) #10
  unreachable
}

; Function Attrs: mustprogress nobuiltin nounwind
define weak dso_local void @_ZdlPv(ptr noundef %0) local_unnamed_addr #8 {
  tail call void @llvm.assigner.free(ptr %0)
  ret void
}

; Function Attrs: mustprogress nobuiltin nounwind
define weak dso_local void @_ZdlPvRKSt9nothrow_t(ptr noundef %0, ptr noundef nonnull align 1 dereferenceable(1) %1) local_unnamed_addr #8 {
  tail call void @_ZdlPv(ptr noundef %0) #3
  ret void
}

; Function Attrs: mustprogress nounwind
define weak dso_local void @_ZdlPvm(ptr noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  tail call void @_ZdlPv(ptr noundef %0) #3
  ret void
}

; Function Attrs: mustprogress nobuiltin nounwind
define weak dso_local void @_ZdaPv(ptr noundef %0) local_unnamed_addr #8 {
  tail call void @_ZdlPv(ptr noundef %0) #3
  ret void
}

; Function Attrs: mustprogress nobuiltin nounwind
define weak dso_local void @_ZdaPvRKSt9nothrow_t(ptr noundef %0, ptr noundef nonnull align 1 dereferenceable(1) %1) local_unnamed_addr #8 {
  tail call void @_ZdaPv(ptr noundef %0) #3
  ret void
}

; Function Attrs: mustprogress nounwind
define weak dso_local void @_ZdaPvm(ptr noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  tail call void @_ZdaPv(ptr noundef %0) #3
  ret void
}

attributes #0 = { mustprogress nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { circuit mustprogress nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nounwind }
attributes #4 = { nounwind allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #5 = { mustprogress nobuiltin allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #6 = { mustprogress nobuiltin nounwind allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #7 = { noinline noreturn nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #8 = { mustprogress nobuiltin nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #9 = { allocsize(0) }
attributes #10 = { noreturn nounwind }

!llvm.ident = !{!0, !0, !0, !0, !0, !0, !0, !0}
!llvm.module.flags = !{!1, !2}

!0 = !{!"clang version 17.0.4 (git@github.com:NilFoundation/zkllvm-circifier.git a425ae939cbdc88d861dbc27ad9adce5da68cf94)"}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{!4, !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
!6 = distinct !{!6, !7, !8}
!7 = !{!"llvm.loop.mustprogress"}
!8 = !{!"llvm.loop.unroll.full"}
!9 = distinct !{!9, !7, !8}
!10 = distinct !{!10, !7, !8}
!11 = distinct !{!11, !7, !8}
!12 = distinct !{!12, !7, !8}
!13 = distinct !{!13, !7, !8}
)BYTECODE";

#endif