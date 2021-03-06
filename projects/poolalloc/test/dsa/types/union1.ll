; ModuleID = 'union1.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"

;RUN: dsaopt %s -dsa-local -analyze -enable-type-inference-opts -check-type=func:s1,FoldedVOID
;RUN: adsaopt %s -mem2reg -mergearrgep -dce -o %t.bc
;RUN: dsaopt %t.bc -dsa-local -analyze -enable-type-inference-opts -check-type=func:s1,0:i32*

%struct.NestedStructType = type { float, i32* }
%struct.StructType = type { float, i32*, %struct.NestedStructType }
%union.UnionType = type { %struct.StructType, [92 x i32] }

define void @func() nounwind {
entry:
  %tmp = alloca i32*                              ; <i32**> [#uses=2]
  %s1 = alloca %union.UnionType                   ; <%union.UnionType*> [#uses=5]
  %c = alloca i32*                                ; <i32**> [#uses=1]
  %d = alloca i32                                 ; <i32*> [#uses=1]
  %arr = alloca i32                               ; <i32*> [#uses=1]
  %x = alloca %struct.StructType                  ; <%struct.StructType*> [#uses=4]
  %y = alloca float                               ; <float*> [#uses=1]
  %"alloca point" = bitcast i32 0 to i32          ; <i32> [#uses=0]
  %0 = call noalias i8* @malloc(i64 4) nounwind   ; <i8*> [#uses=1]
  %1 = bitcast i8* %0 to i32*                     ; <i32*> [#uses=1]
  store i32* %1, i32** %tmp, align 8
  %2 = getelementptr inbounds %union.UnionType* %s1, i32 0, i32 0 ; <%struct.StructType*> [#uses=1]
  %3 = bitcast %struct.StructType* %2 to i32**    ; <i32**> [#uses=1]
  %4 = load i32** %tmp, align 8                   ; <i32*> [#uses=1]
  store i32* %4, i32** %3, align 8
  %5 = getelementptr inbounds %union.UnionType* %s1, i32 0, i32 0 ; <%struct.StructType*> [#uses=1]
  %6 = bitcast %struct.StructType* %5 to i32**    ; <i32**> [#uses=1]
  %7 = load i32** %6, align 8                     ; <i32*> [#uses=1]
  store i32* %7, i32** %c, align 8
  %8 = getelementptr inbounds %union.UnionType* %s1, i32 0, i32 0 ; <%struct.StructType*> [#uses=1]
  %9 = bitcast %struct.StructType* %8 to i32*     ; <i32*> [#uses=1]
  %10 = load i32* %9, align 8                     ; <i32> [#uses=1]
  store i32 %10, i32* %d, align 4
  %11 = getelementptr inbounds %union.UnionType* %s1, i32 0, i32 0 ; <%struct.StructType*> [#uses=1]
  %12 = bitcast %struct.StructType* %11 to [100 x i32]* ; <[100 x i32]*> [#uses=1]
  %13 = getelementptr inbounds [100 x i32]* %12, i64 0, i64 0 ; <i32*> [#uses=1]
  %14 = load i32* %13, align 4                    ; <i32> [#uses=1]
  store i32 %14, i32* %arr, align 4
  %15 = getelementptr inbounds %union.UnionType* %s1, i32 0, i32 0 ; <%struct.StructType*> [#uses=3]
  %16 = getelementptr inbounds %struct.StructType* %x, i32 0, i32 0 ; <float*> [#uses=1]
  %17 = getelementptr inbounds %struct.StructType* %15, i32 0, i32 0 ; <float*> [#uses=1]
  %18 = load float* %17, align 8                  ; <float> [#uses=1]
  store float %18, float* %16, align 8
  %19 = getelementptr inbounds %struct.StructType* %x, i32 0, i32 1 ; <i32**> [#uses=1]
  %20 = getelementptr inbounds %struct.StructType* %15, i32 0, i32 1 ; <i32**> [#uses=1]
  %21 = load i32** %20, align 8                   ; <i32*> [#uses=1]
  store i32* %21, i32** %19, align 8
  %22 = getelementptr inbounds %struct.StructType* %x, i32 0, i32 2 ; <%struct.NestedStructType*> [#uses=2]
  %23 = getelementptr inbounds %struct.StructType* %15, i32 0, i32 2 ; <%struct.NestedStructType*> [#uses=2]
  %24 = getelementptr inbounds %struct.NestedStructType* %22, i32 0, i32 0 ; <float*> [#uses=1]
  %25 = getelementptr inbounds %struct.NestedStructType* %23, i32 0, i32 0 ; <float*> [#uses=1]
  %26 = load float* %25, align 8                  ; <float> [#uses=1]
  store float %26, float* %24, align 8
  %27 = getelementptr inbounds %struct.NestedStructType* %22, i32 0, i32 1 ; <i32**> [#uses=1]
  %28 = getelementptr inbounds %struct.NestedStructType* %23, i32 0, i32 1 ; <i32**> [#uses=1]
  %29 = load i32** %28, align 8                   ; <i32*> [#uses=1]
  store i32* %29, i32** %27, align 8
  %30 = getelementptr inbounds %struct.StructType* %x, i32 0, i32 0 ; <float*> [#uses=1]
  %31 = load float* %30, align 8                  ; <float> [#uses=1]
  store float %31, float* %y, align 4
  br label %return

return:                                           ; preds = %entry
  ret void
}

declare noalias i8* @malloc(i64) nounwind
