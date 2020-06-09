[@deriving sexp]
type edit_state = (ZExp.t, HTyp.t, MetaVarGen.t);

type type_mode =
  | Syn
  | Ana(HTyp.t);

let tuple_zip =
    (
      ~get_tuple_elements: Skel.t('op) => list(Skel.t('op)),
      skel: Skel.t('op),
      ty: HTyp.t,
    )
    : option(list((Skel.t('op), HTyp.t))) => {
  let skels = skel |> get_tuple_elements;
  let tys = ty |> HTyp.get_prod_elements;
  switch (ListUtil.opt_zip(skels, tys)) {
  | Some(_) as zipped => zipped
  | None =>
    switch (skels, tys) {
    | ([_], _) => Some([(skel, ty)])
    | (_, [Hole]) =>
      skels |> List.map(skel => (skel, HTyp.Hole)) |> Option.some
    | _ => None
    }
  };
};

module Pat = {
  let tuple_zip = tuple_zip(~get_tuple_elements=UHPat.get_tuple_elements);

  let rec syn = (ctx: Contexts.t, p: UHPat.t): option((HTyp.t, Contexts.t)) =>
    syn_opseq(ctx, p)
  and syn_opseq =
      (ctx: Contexts.t, OpSeq(skel, seq): UHPat.opseq)
      : option((HTyp.t, Contexts.t)) =>
    syn_skel(ctx, skel, seq)
  and syn_skel =
      (ctx: Contexts.t, skel: UHPat.skel, seq: UHPat.seq)
      : option((HTyp.t, Contexts.t)) =>
    switch (skel) {
    | Placeholder(n) =>
      let pn = seq |> Seq.nth_operand(n);
      syn_operand(ctx, pn);
    | BinOp(InHole(_), op, skel1, skel2) =>
      let skel_not_in_hole = Skel.BinOp(NotInHole, op, skel1, skel2);
      switch (syn_skel(ctx, skel_not_in_hole, seq)) {
      | None => None
      | Some((_, ctx)) => Some((HTyp.Hole, ctx))
      };
    | BinOp(NotInHole, Comma, _, _) =>
      skel
      |> UHPat.get_tuple_elements
      |> ListUtil.map_with_accumulator_opt(
           (ctx, skel) =>
             syn_skel(ctx, skel, seq) |> Option.map(TupleUtil.swap),
           ctx,
         )
      |> Option.map(((ctx, tys)) => (HTyp.Prod(tys), ctx))
    | BinOp(NotInHole, Space, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, HTyp.Hole)) {
      | None => None
      | Some(ctx) =>
        switch (ana_skel(ctx, skel2, seq, HTyp.Hole)) {
        | None => None
        | Some(ctx) => Some((Hole, ctx))
        }
      }
    | BinOp(NotInHole, Cons, skel1, skel2) =>
      switch (syn_skel(ctx, skel1, seq)) {
      | None => None
      | Some((ty1, ctx)) =>
        let ty = HTyp.List(ty1);
        switch (ana_skel(ctx, skel2, seq, ty)) {
        | None => None
        | Some(ctx) => Some((ty, ctx))
        };
      }
    }
  and syn_operand =
      (ctx: Contexts.t, operand: UHPat.operand)
      : option((HTyp.t, Contexts.t)) =>
    switch (operand) {
    /* in hole */
    | EmptyHole(_) => Some((Hole, ctx))
    | Wild(InHole(TypeInconsistent, _))
    | Var(InHole(TypeInconsistent, _), _, _)
    | IntLit(InHole(TypeInconsistent, _), _)
    | FloatLit(InHole(TypeInconsistent, _), _)
    | BoolLit(InHole(TypeInconsistent, _), _)
    | StringLit(InHole(TypeInconsistent, _), _)
    | ListNil(InHole(TypeInconsistent, _))
    | Inj(InHole(TypeInconsistent, _), _, _) =>
      let operand' = UHPat.set_err_status_operand(NotInHole, operand);
      syn_operand(ctx, operand')
      |> OptUtil.map(((_, gamma)) => (HTyp.Hole, gamma));
    | Wild(InHole(WrongLength, _))
    | Var(InHole(WrongLength, _), _, _)
    | IntLit(InHole(WrongLength, _), _)
    | FloatLit(InHole(WrongLength, _), _)
    | BoolLit(InHole(WrongLength, _), _)
    | StringLit(InHole(WrongLength, _), _)
    | ListNil(InHole(WrongLength, _))
    | Inj(InHole(WrongLength, _), _, _) => None
    /* not in hole */
    | Wild(NotInHole) => Some((Hole, ctx))
    | Var(NotInHole, InVarHole(Free, _), _) => raise(UHPat.FreeVarInPat)
    | Var(NotInHole, InVarHole(Keyword(_), _), _) => Some((Hole, ctx))
    | Var(NotInHole, NotInVarHole, x) =>
      Var.check_valid(
        x,
        Some((HTyp.Hole, Contexts.extend_gamma(ctx, (x, Hole)))),
      )
    | IntLit(NotInHole, _) => Some((Int, ctx))
    | FloatLit(NotInHole, _) => Some((Float, ctx))
    | BoolLit(NotInHole, _) => Some((Bool, ctx))
    | StringLit(NotInHole, _) => Some((String, ctx))
    | ListNil(NotInHole) => Some((List(Hole), ctx))
    | Inj(NotInHole, inj_side, p1) =>
      switch (syn(ctx, p1)) {
      | None => None
      | Some((ty1, ctx)) =>
        let ty =
          switch (inj_side) {
          | L => HTyp.Sum(ty1, Hole)
          | R => HTyp.Sum(Hole, ty1)
          };
        Some((ty, ctx));
      }
    | Parenthesized(p) => syn(ctx, p)
    }
  and ana = (ctx: Contexts.t, p: UHPat.t, ty: HTyp.t): option(Contexts.t) =>
    ana_opseq(ctx, p, ty)
  and ana_opseq =
      (ctx: Contexts.t, OpSeq(skel, seq) as opseq: UHPat.opseq, ty: HTyp.t)
      : option(Contexts.t) =>
    switch (tuple_zip(skel, ty)) {
    | None =>
      switch (UHPat.get_err_status_opseq(opseq), HTyp.get_prod_elements(ty)) {
      | (InHole(TypeInconsistent, _), [_])
      | (InHole(WrongLength, _), _) =>
        let opseq' = opseq |> UHPat.set_err_status_opseq(NotInHole);
        syn_opseq(ctx, opseq') |> OptUtil.map(_ => ctx);
      | _ => None
      }
    | Some(skel_tys) =>
      skel_tys
      |> List.fold_left(
           (acc: option(Contexts.t), (skel, ty)) =>
             switch (acc) {
             | None => None
             | Some(ctx) => ana_skel(ctx, skel, seq, ty)
             },
           Some(ctx),
         )
    }
  and ana_skel =
      (ctx: Contexts.t, skel: UHPat.skel, seq: UHPat.seq, ty: HTyp.t)
      : option(Contexts.t) =>
    switch (skel) {
    | BinOp(_, Comma, _, _)
    | BinOp(InHole(WrongLength, _), _, _, _) =>
      failwith("Pat.ana_skel: expected tuples to be handled at opseq level")
    | Placeholder(n) =>
      let pn = Seq.nth_operand(n, seq);
      ana_operand(ctx, pn, ty);
    | BinOp(InHole(TypeInconsistent, _), op, skel1, skel2) =>
      let skel_not_in_hole = Skel.BinOp(NotInHole, op, skel1, skel2);
      switch (syn_skel(ctx, skel_not_in_hole, seq)) {
      | None => None
      | Some((_, ctx)) => Some(ctx)
      };
    | BinOp(NotInHole, Space, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, HTyp.Hole)) {
      | None => None
      | Some(ctx) => ana_skel(ctx, skel2, seq, HTyp.Hole)
      }
    | BinOp(NotInHole, Cons, skel1, skel2) =>
      switch (HTyp.matched_list(ty)) {
      | None => None
      | Some(ty_elt) =>
        switch (ana_skel(ctx, skel1, seq, ty_elt)) {
        | None => None
        | Some(ctx) => ana_skel(ctx, skel2, seq, HTyp.List(ty_elt))
        }
      }
    }
  and ana_operand =
      (ctx: Contexts.t, operand: UHPat.operand, ty: HTyp.t)
      : option(Contexts.t) =>
    switch (operand) {
    /* in hole */
    | EmptyHole(_) => Some(ctx)
    | Wild(InHole(TypeInconsistent, _))
    | Var(InHole(TypeInconsistent, _), _, _)
    | IntLit(InHole(TypeInconsistent, _), _)
    | FloatLit(InHole(TypeInconsistent, _), _)
    | BoolLit(InHole(TypeInconsistent, _), _)
    | StringLit(InHole(TypeInconsistent, _), _)
    | ListNil(InHole(TypeInconsistent, _))
    | Inj(InHole(TypeInconsistent, _), _, _) =>
      let operand' = UHPat.set_err_status_operand(NotInHole, operand);
      syn_operand(ctx, operand') |> OptUtil.map(((_, ctx)) => ctx);
    | Wild(InHole(WrongLength, _))
    | Var(InHole(WrongLength, _), _, _)
    | IntLit(InHole(WrongLength, _), _)
    | FloatLit(InHole(WrongLength, _), _)
    | BoolLit(InHole(WrongLength, _), _)
    | StringLit(InHole(WrongLength, _), _)
    | ListNil(InHole(WrongLength, _))
    | Inj(InHole(WrongLength, _), _, _) =>
      ty |> HTyp.get_prod_elements |> List.length > 1 ? Some(ctx) : None
    /* not in hole */
    | Var(NotInHole, InVarHole(Free, _), _) => raise(UHPat.FreeVarInPat)
    | Var(NotInHole, InVarHole(Keyword(_), _), _) => Some(ctx)
    | Var(NotInHole, NotInVarHole, x) =>
      Var.check_valid(x, Some(Contexts.extend_gamma(ctx, (x, ty))))
    | Wild(NotInHole) => Some(ctx)
    | IntLit(NotInHole, _)
    | FloatLit(NotInHole, _)
    | BoolLit(NotInHole, _)
    | StringLit(NotInHole, _) =>
      switch (syn_operand(ctx, operand)) {
      | None => None
      | Some((ty', ctx')) =>
        if (HTyp.consistent(ty, ty')) {
          Some(ctx');
        } else {
          None;
        }
      }
    | ListNil(NotInHole) =>
      switch (HTyp.matched_list(ty)) {
      | None => None
      | Some(_) => Some(ctx)
      }
    | Inj(NotInHole, side, p1) =>
      switch (HTyp.matched_sum(ty)) {
      | None => None
      | Some((tyL, tyR)) =>
        let ty1 = InjSide.pick(side, tyL, tyR);
        ana(ctx, p1, ty1);
      }
    | Parenthesized(p) => ana(ctx, p, ty)
    };

  /**
   * Get type mode of nth operand of an opseq in synthetic position
   */
  let rec syn_nth_type_mode =
          (ctx: Contexts.t, n: int, OpSeq(skel, seq): UHPat.opseq)
          : option(type_mode) =>
    syn_nth_type_mode'(ctx, n, skel, seq)
  and syn_nth_type_mode' =
      (ctx: Contexts.t, n: int, skel: UHPat.skel, seq: UHPat.seq)
      : option(type_mode) => {
    let ana_go = (skel, ty) => ana_nth_type_mode'(ctx, n, skel, seq, ty);
    let rec go = (skel: UHPat.skel) =>
      switch (skel) {
      | Placeholder(n') =>
        assert(n == n');
        Some(Syn);
      | BinOp(InHole(_), op, skel1, skel2) =>
        go(BinOp(NotInHole, op, skel1, skel2))
      | BinOp(NotInHole, Comma, skel1, skel2) =>
        n <= Skel.rightmost_tm_index(skel1) ? go(skel1) : go(skel2)
      | BinOp(NotInHole, Space, skel1, skel2) =>
        switch (syn_skel(ctx, skel1, seq)) {
        | None => None
        | Some((ty1, _)) =>
          if (n <= Skel.rightmost_tm_index(skel1)) {
            go(skel1);
          } else {
            switch (HTyp.matched_arrow(ty1)) {
            | None => None
            | Some((ty2, _)) => ana_go(skel2, ty2)
            };
          }
        }
      | BinOp(NotInHole, Cons, skel1, skel2) =>
        switch (syn_skel(ctx, skel1, seq)) {
        | None => None
        | Some((ty1, _)) =>
          n <= Skel.rightmost_tm_index(skel1)
            ? go(skel1) : ana_go(skel2, HTyp.List(ty1))
        }
      };
    go(skel);
  }
  /**
   * Get type mode of nth operand of an opseq in analytic position
   */
  and ana_nth_type_mode =
      (
        ctx: Contexts.t,
        n: int,
        OpSeq(skel, seq) as opseq: UHPat.opseq,
        ty: HTyp.t,
      )
      : option(type_mode) => {
    // handle n-tuples
    switch (tuple_zip(skel, ty)) {
    | None =>
      syn_nth_type_mode(
        ctx,
        n,
        opseq |> UHPat.set_err_status_opseq(NotInHole),
      )
    | Some(skel_tys) =>
      let (nskel, nty) =
        skel_tys
        |> List.find(((skel, _)) =>
             Skel.leftmost_tm_index(skel) <= n
             && n <= Skel.rightmost_tm_index(skel)
           );
      ana_nth_type_mode'(ctx, n, nskel, seq, nty);
    };
  }
  and ana_nth_type_mode' =
      (ctx: Contexts.t, n: int, skel: UHPat.skel, seq: UHPat.seq, ty: HTyp.t)
      : option(type_mode) => {
    let rec go = (skel: UHPat.skel, ty: HTyp.t) =>
      switch (skel) {
      | BinOp(_, Comma, _, _)
      | BinOp(InHole(WrongLength, _), _, _, _) =>
        failwith(__LOC__ ++ ": expected tuples to be handled at opseq level")
      | Placeholder(n') =>
        assert(n == n');
        Some(Ana(ty));
      | BinOp(InHole(TypeInconsistent, _), op, skel1, skel2) =>
        let skel_not_in_hole = Skel.BinOp(NotInHole, op, skel1, skel2);
        syn_nth_type_mode'(ctx, n, skel_not_in_hole, seq);
      | BinOp(NotInHole, Space, skel1, skel2) =>
        n <= Skel.rightmost_tm_index(skel1)
          ? go(skel1, HTyp.Hole) : go(skel2, HTyp.Hole)
      | BinOp(NotInHole, Cons, skel1, skel2) =>
        switch (HTyp.matched_list(ty)) {
        | None => None
        | Some(ty_elt) =>
          n <= Skel.rightmost_tm_index(skel1)
            ? go(skel1, ty_elt) : go(skel2, ty)
        }
      };
    go(skel, ty);
  };

  let rec syn_fix_holes =
          (
            ctx: Contexts.t,
            u_gen: MetaVarGen.t,
            ~renumber_empty_holes=false,
            p: UHPat.t,
          )
          : (UHPat.t, HTyp.t, Contexts.t, MetaVarGen.t) =>
    syn_fix_holes_opseq(ctx, u_gen, ~renumber_empty_holes, p)
  and syn_fix_holes_opseq =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        OpSeq(skel, seq): UHPat.opseq,
      )
      : (UHPat.opseq, HTyp.t, Contexts.t, MetaVarGen.t) => {
    let (skel, seq, ty, ctx, u_gen) =
      syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel, seq);
    (OpSeq(skel, seq), ty, ctx, u_gen);
  }
  and syn_fix_holes_skel =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        skel: UHPat.skel,
        seq: UHPat.seq,
      )
      : (UHPat.skel, UHPat.seq, HTyp.t, Contexts.t, MetaVarGen.t) =>
    switch (skel) {
    | Placeholder(n) =>
      let pn = Seq.nth_operand(n, seq);
      let (pn, ty, ctx, u_gen) =
        syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, pn);
      let seq = seq |> Seq.update_nth_operand(n, pn);
      (skel, seq, ty, ctx, u_gen);
    | BinOp(_, Comma, _, _) =>
      let ((ctx, u_gen, seq), pairs) =
        skel
        |> UHPat.get_tuple_elements
        |> ListUtil.map_with_accumulator(
             ((ctx, u_gen, seq), skel) => {
               let (skel, seq, ty, ctx, u_gen) =
                 syn_fix_holes_skel(
                   ctx,
                   u_gen,
                   ~renumber_empty_holes,
                   skel,
                   seq,
                 );
               ((ctx, u_gen, seq), (skel, ty));
             },
             (ctx, u_gen, seq),
           );
      let (skels, tys) = List.split(pairs);
      (UHPat.mk_tuple(skels), seq, Prod(tys), ctx, u_gen);
    | BinOp(_, Space, skel1, skel2) =>
      let (skel1, seq, ctx, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Hole,
        );
      let (skel2, seq, ctx, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Hole,
        );
      let (u, u_gen) = MetaVarGen.next(u_gen);
      let skel =
        Skel.BinOp(
          InHole(TypeInconsistent, u),
          Operators.Pat.Space,
          skel1,
          skel2,
        );
      let ty = HTyp.Hole;
      (skel, seq, ty, ctx, u_gen);
    | BinOp(_, Cons, skel1, skel2) =>
      let (skel1, seq, ty_elt, ctx, u_gen) =
        syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel1, seq);
      let ty = HTyp.List(ty_elt);
      let (skel2, seq, ctx, u_gen) =
        ana_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel2, seq, ty);
      let skel = Skel.BinOp(NotInHole, Operators.Pat.Cons, skel1, skel2);
      (skel, seq, ty, ctx, u_gen);
    }
  and syn_fix_holes_operand =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        operand: UHPat.operand,
      )
      : (UHPat.operand, HTyp.t, Contexts.t, MetaVarGen.t) => {
    let operand_nih = operand |> UHPat.set_err_status_operand(NotInHole);
    switch (operand) {
    | EmptyHole(_) =>
      if (renumber_empty_holes) {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (EmptyHole(u), Hole, ctx, u_gen);
      } else {
        (operand, HTyp.Hole, ctx, u_gen);
      }
    | Wild(_) => (operand_nih, Hole, ctx, u_gen)
    | Var(_, InVarHole(Free, _), _) => raise(UHPat.FreeVarInPat)
    | Var(_, InVarHole(Keyword(_), _), _) => (operand_nih, Hole, ctx, u_gen)
    | Var(_, NotInVarHole, x) =>
      let ctx = Contexts.extend_gamma(ctx, (x, Hole));
      (operand_nih, Hole, ctx, u_gen);
    | IntLit(_, _) => (operand_nih, Int, ctx, u_gen)
    | FloatLit(_, _) => (operand_nih, Float, ctx, u_gen)
    | BoolLit(_, _) => (operand_nih, Bool, ctx, u_gen)
    | StringLit(_, _) => (operand_nih, String, ctx, u_gen)
    | ListNil(_) => (operand_nih, List(Hole), ctx, u_gen)
    | Parenthesized(p) =>
      let (p, ty, ctx, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, p);
      (Parenthesized(p), ty, ctx, u_gen);
    | Inj(_, side, p1) =>
      let (p1, ty1, ctx, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, p1);
      let p = UHPat.Inj(NotInHole, side, p1);
      let ty =
        switch (side) {
        | L => HTyp.Sum(ty1, Hole)
        | R => HTyp.Sum(Hole, ty1)
        };
      (p, ty, ctx, u_gen);
    };
  }
  and ana_fix_holes =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        p: UHPat.t,
        ty: HTyp.t,
      )
      : (UHPat.t, Contexts.t, MetaVarGen.t) =>
    ana_fix_holes_opseq(ctx, u_gen, ~renumber_empty_holes, p, ty)
  and ana_fix_holes_opseq =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        OpSeq(skel, seq) as opseq: UHPat.opseq,
        ty: HTyp.t,
      )
      : (UHPat.opseq, Contexts.t, MetaVarGen.t) => {
    // handle n-tuples
    switch (tuple_zip(skel, ty)) {
    | Some(skel_tys) =>
      skel_tys
      |> List.fold_left(
           (
             (
               rev_skels: list(UHPat.skel),
               seq: UHPat.seq,
               ctx: Contexts.t,
               u_gen: MetaVarGen.t,
             ),
             (skel: UHPat.skel, ty: HTyp.t),
           ) => {
             let (skel, seq, ctx, u_gen) =
               ana_fix_holes_skel(
                 ctx,
                 u_gen,
                 ~renumber_empty_holes,
                 skel,
                 seq,
                 ty,
               );
             ([skel, ...rev_skels], seq, ctx, u_gen);
           },
           ([], seq, ctx, u_gen),
         )
      |> (
        fun
        | (rev_skels, seq, ctx, u_gen) => {
            let skel = rev_skels |> List.rev |> UHPat.mk_tuple;
            (OpSeq.OpSeq(skel, seq), ctx, u_gen);
          }
      )
    | None =>
      if (List.length(HTyp.get_prod_elements(ty)) == 1) {
        skel
        |> UHPat.get_tuple_elements
        |> List.fold_left(
             (
               (
                 rev_skels: list(UHPat.skel),
                 seq: UHPat.seq,
                 ctx: Contexts.t,
                 u_gen: MetaVarGen.t,
               ),
               skel: UHPat.skel,
             ) => {
               let (skel, seq, _, ctx, u_gen) =
                 syn_fix_holes_skel(
                   ctx,
                   u_gen,
                   ~renumber_empty_holes,
                   skel,
                   seq,
                 );
               ([skel, ...rev_skels], seq, ctx, u_gen);
             },
             ([], seq, ctx, u_gen),
           )
        |> (
          fun
          | (rev_skels, seq, ctx, u_gen) => {
              let (u, u_gen) = MetaVarGen.next(u_gen);
              let skel = UHPat.mk_tuple(List.rev(rev_skels));
              let opseq =
                UHPat.set_err_status_opseq(
                  InHole(TypeInconsistent, u),
                  OpSeq.OpSeq(skel, seq),
                );
              (opseq, ctx, u_gen);
            }
        );
      } else {
        let (u, u_gen) = u_gen |> MetaVarGen.next;
        let (opseq, _, _, u_gen) =
          syn_fix_holes_opseq(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            opseq |> UHPat.set_err_status_opseq(NotInHole),
          );
        (
          opseq |> UHPat.set_err_status_opseq(InHole(WrongLength, u)),
          ctx,
          u_gen,
        );
      }
    };
  }
  and ana_fix_holes_skel =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        skel: UHPat.skel,
        seq: UHPat.seq,
        ty: HTyp.t,
      )
      : (UHPat.skel, UHPat.seq, Contexts.t, MetaVarGen.t) =>
    switch (skel) {
    | BinOp(_, Comma, _, _) =>
      failwith("Pat.ana_fix_holes_skel: tuples handled at opseq level")
    | Placeholder(n) =>
      let pn = Seq.nth_operand(n, seq);
      let (pn, ctx, u_gen) =
        ana_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, pn, ty);
      let seq = seq |> Seq.update_nth_operand(n, pn);
      (skel, seq, ctx, u_gen);
    | BinOp(_, Space, skel1, skel2) =>
      let (skel1, seq, ctx, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Hole,
        );
      let (skel2, seq, ctx, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Hole,
        );
      let (u, u_gen) = MetaVarGen.next(u_gen);
      let skel =
        Skel.BinOp(
          InHole(TypeInconsistent, u),
          Operators.Pat.Space,
          skel1,
          skel2,
        );
      (skel, seq, ctx, u_gen);
    | BinOp(_, Cons, skel1, skel2) =>
      switch (HTyp.matched_list(ty)) {
      | Some(ty_elt) =>
        let (skel1, seq, ctx, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel1,
            seq,
            ty_elt,
          );
        let ty_list = HTyp.List(ty_elt);
        let (skel2, seq, ctx, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel2,
            seq,
            ty_list,
          );
        let skel = Skel.BinOp(NotInHole, Operators.Pat.Cons, skel1, skel2);
        (skel, seq, ctx, u_gen);
      | None =>
        let (skel1, seq, ty_elt, ctx, u_gen) =
          syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel1, seq);
        let ty_list = HTyp.List(ty_elt);
        let (skel2, seq, ctx, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel2,
            seq,
            ty_list,
          );
        let (u, u_gen) = MetaVarGen.next(u_gen);
        let skel =
          Skel.BinOp(
            InHole(TypeInconsistent, u),
            Operators.Pat.Cons,
            skel1,
            skel2,
          );
        (skel, seq, ctx, u_gen);
      }
    }
  and ana_fix_holes_operand =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        operand: UHPat.operand,
        ty: HTyp.t,
      )
      : (UHPat.operand, Contexts.t, MetaVarGen.t) => {
    let operand_nih = UHPat.set_err_status_operand(NotInHole, operand);
    switch (operand) {
    | EmptyHole(_) =>
      if (renumber_empty_holes) {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (EmptyHole(u), ctx, u_gen);
      } else {
        (operand, ctx, u_gen);
      }
    | Wild(_) => (operand_nih, ctx, u_gen)
    | Var(_, InVarHole(Free, _), _) => raise(UHPat.FreeVarInPat)
    | Var(_, InVarHole(Keyword(_), _), _) => (operand_nih, ctx, u_gen)
    | Var(_, NotInVarHole, x) =>
      let ctx = Contexts.extend_gamma(ctx, (x, ty));
      (operand_nih, ctx, u_gen);
    | IntLit(_, _)
    | FloatLit(_, _)
    | BoolLit(_, _)
    | StringLit(_) =>
      let (operand', ty', ctx, u_gen) =
        syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, operand);
      if (HTyp.consistent(ty, ty')) {
        (UHPat.set_err_status_operand(NotInHole, operand'), ctx, u_gen);
      } else {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (
          UHPat.set_err_status_operand(InHole(TypeInconsistent, u), operand'),
          ctx,
          u_gen,
        );
      };
    | ListNil(_) =>
      switch (HTyp.matched_list(ty)) {
      | Some(_) => (ListNil(NotInHole), ctx, u_gen)
      | None =>
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (ListNil(InHole(TypeInconsistent, u)), ctx, u_gen);
      }
    | Parenthesized(p1) =>
      let (p1, ctx, u_gen) =
        ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p1, ty);
      (Parenthesized(p1), ctx, u_gen);
    | Inj(_, side, p1) =>
      switch (HTyp.matched_sum(ty)) {
      | Some((tyL, tyR)) =>
        let ty1 = InjSide.pick(side, tyL, tyR);
        let (p1, ctx, u_gen) =
          ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p1, ty1);
        (Inj(NotInHole, side, p1), ctx, u_gen);
      | None =>
        let (p1, _, ctx, u_gen) =
          syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, p1);
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (Inj(InHole(TypeInconsistent, u), side, p1), ctx, u_gen);
      }
    };
  };

  let syn_fix_holes_z =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, zp: ZPat.t)
      : (ZPat.t, HTyp.t, Contexts.t, MetaVarGen.t) => {
    let path = CursorPath.Pat.of_z(zp);
    let (p, ty, ctx, u_gen) = syn_fix_holes(ctx, u_gen, ZPat.erase(zp));
    let zp =
      CursorPath.Pat.follow(path, p)
      |> OptUtil.get(() =>
           failwith(
             "syn_fix_holes did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (zp, ty, ctx, u_gen);
  };

  let ana_fix_holes_z =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, zp: ZPat.t, ty: HTyp.t)
      : (ZPat.t, Contexts.t, MetaVarGen.t) => {
    let path = CursorPath.Pat.of_z(zp);
    let (p, ctx, u_gen) = ana_fix_holes(ctx, u_gen, ZPat.erase(zp), ty);
    let zp =
      CursorPath.Pat.follow(path, p)
      |> OptUtil.get(() =>
           failwith(
             "ana_fix_holes did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (zp, ctx, u_gen);
  };
};

module Exp = {
  let tuple_zip = tuple_zip(~get_tuple_elements=UHExp.get_tuple_elements);

  /* returns recursive ctx + name of recursively defined var */
  let ctx_for_let' =
      (ctx: Contexts.t, p: UHPat.t, ty: HTyp.t, e: UHExp.t)
      : (Contexts.t, option(Var.t)) =>
    switch (p, e) {
    | (
        OpSeq(_, S(Var(_, NotInVarHole, x), E)),
        [ExpLine(OpSeq(_, S(Lam(_), E)))],
      ) =>
      switch (HTyp.matched_arrow(ty)) {
      | Some(_) => (Contexts.extend_gamma(ctx, (x, ty)), Some(x))
      | None => (ctx, None)
      }
    | _ => (ctx, None)
    };

  let ctx_for_let =
      (ctx: Contexts.t, p: UHPat.t, ty: HTyp.t, e: UHExp.t): Contexts.t => {
    let (ctx, _) = ctx_for_let'(ctx, p, ty, e);
    ctx;
  };

  /**
   * Synthesize a type, if possible, for e
   */
  let rec syn = (ctx: Contexts.t, e: UHExp.t): option(HTyp.t) =>
    syn_block(ctx, e)
  and syn_block = (ctx: Contexts.t, block: UHExp.block): option(HTyp.t) =>
    switch (block |> UHExp.Block.split_conclusion) {
    | None => None
    | Some((leading, conclusion)) =>
      switch (syn_lines(ctx, leading)) {
      | None => None
      | Some(ctx) => syn_opseq(ctx, conclusion)
      }
    }
  and syn_lines =
      (ctx: Contexts.t, lines: list(UHExp.line)): option(Contexts.t) => {
    lines
    |> List.fold_left(
         (opt_ctx: option(Contexts.t), line: UHExp.line) =>
           switch (opt_ctx) {
           | None => None
           | Some(ctx) => syn_line(ctx, line)
           },
         Some(ctx),
       );
  }
  and syn_line = (ctx: Contexts.t, line: UHExp.line): option(Contexts.t) =>
    switch (line) {
    | ExpLine(opseq) => syn_opseq(ctx, opseq) |> OptUtil.map(_ => ctx)
    | EmptyLine => Some(ctx)
    | LetLine(p, ann, def) =>
      switch (ann) {
      | Some(uty) =>
        let ty = UHTyp.expand(uty);
        let ctx_def = ctx_for_let(ctx, p, ty, def);
        switch (ana(ctx_def, def, ty)) {
        | None => None
        | Some(_) => Pat.ana(ctx, p, ty)
        };
      | None =>
        switch (syn(ctx, def)) {
        | None => None
        | Some(ty) => Pat.ana(ctx, p, ty)
        }
      }
    }
  and syn_opseq =
      (ctx: Contexts.t, OpSeq(skel, seq): UHExp.opseq): option(HTyp.t) =>
    syn_skel(ctx, skel, seq)
  and syn_skel =
      (ctx: Contexts.t, skel: UHExp.skel, seq: UHExp.seq): option(HTyp.t) =>
    switch (skel) {
    | Placeholder(n) =>
      let en = Seq.nth_operand(n, seq);
      syn_operand(ctx, en);
    | BinOp(InHole(_), op, skel1, skel2) =>
      let skel_not_in_hole = Skel.BinOp(NotInHole, op, skel1, skel2);
      syn_skel(ctx, skel_not_in_hole, seq) |> OptUtil.map(_ => HTyp.Hole);
    | BinOp(NotInHole, Minus | Plus | Times | Divide, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, HTyp.Int)) {
      | None => None
      | Some(_) =>
        ana_skel(ctx, skel2, seq, Int) |> OptUtil.map(_ => HTyp.Int)
      }
    | BinOp(NotInHole, FMinus | FPlus | FTimes | FDivide, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, HTyp.Float)) {
      | None => None
      | Some(_) =>
        ana_skel(ctx, skel2, seq, Float) |> OptUtil.map(_ => HTyp.Float)
      }
    | BinOp(NotInHole, And | Or, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, HTyp.Bool)) {
      | None => None
      | Some(_) =>
        ana_skel(ctx, skel2, seq, HTyp.Bool) |> OptUtil.map(_ => HTyp.Bool)
      }
    | BinOp(NotInHole, Caret, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, HTyp.String)) {
      | None => None
      | Some(_) =>
        ana_skel(ctx, skel2, seq, HTyp.String)
        |> OptUtil.map(_ => HTyp.String)
      }
    | BinOp(NotInHole, LessThan | GreaterThan | Equals, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, Int)) {
      | None => None
      | Some(_) =>
        ana_skel(ctx, skel2, seq, Int) |> OptUtil.map(_ => HTyp.Bool)
      }
    | BinOp(NotInHole, FLessThan | FGreaterThan | FEquals, skel1, skel2) =>
      switch (ana_skel(ctx, skel1, seq, Float)) {
      | None => None
      | Some(_) =>
        ana_skel(ctx, skel2, seq, Float) |> OptUtil.map(_ => HTyp.Bool)
      }
    | BinOp(NotInHole, Space, skel1, skel2) =>
      switch (syn_skel(ctx, skel1, seq)) {
      | None => None
      | Some(ty1) =>
        switch (HTyp.matched_arrow(ty1)) {
        | None => None
        | Some((ty2, ty)) =>
          ana_skel(ctx, skel2, seq, ty2) |> OptUtil.map(_ => ty)
        }
      }
    | BinOp(NotInHole, Comma, _, _) =>
      skel
      |> UHExp.get_tuple_elements
      |> List.map(skel => syn_skel(ctx, skel, seq))
      |> OptUtil.sequence
      |> Option.map(tys => HTyp.Prod(tys))
    | BinOp(NotInHole, Cons, skel1, skel2) =>
      switch (syn_skel(ctx, skel1, seq)) {
      | None => None
      | Some(ty1) =>
        let ty = HTyp.List(ty1);
        ana_skel(ctx, skel2, seq, ty) |> OptUtil.map(_ => ty);
      }
    }
  and syn_operand = (ctx: Contexts.t, operand: UHExp.operand): option(HTyp.t) =>
    switch (operand) {
    /* in hole */
    | EmptyHole(_) => Some(Hole)
    | Var(InHole(TypeInconsistent, _), _, _)
    | IntLit(InHole(TypeInconsistent, _), _)
    | FloatLit(InHole(TypeInconsistent, _), _)
    | BoolLit(InHole(TypeInconsistent, _), _)
    | StringLit(InHole(TypeInconsistent, _), _)
    | ListNil(InHole(TypeInconsistent, _))
    | Lam(InHole(TypeInconsistent, _), _, _, _)
    | Inj(InHole(TypeInconsistent, _), _, _)
    | Case(StandardErrStatus(InHole(TypeInconsistent, _)), _, _)
    | ApPalette(InHole(TypeInconsistent, _), _, _, _)
    | Subscript(InHole(TypeInconsistent, _), _, _, _) =>
      let operand' = UHExp.set_err_status_operand(NotInHole, operand);
      syn_operand(ctx, operand') |> OptUtil.map(_ => HTyp.Hole);
    | Var(InHole(WrongLength, _), _, _)
    | IntLit(InHole(WrongLength, _), _)
    | FloatLit(InHole(WrongLength, _), _)
    | BoolLit(InHole(WrongLength, _), _)
    | StringLit(InHole(WrongLength, _), _)
    | ListNil(InHole(WrongLength, _))
    | Lam(InHole(WrongLength, _), _, _, _)
    | Inj(InHole(WrongLength, _), _, _)
    | Case(StandardErrStatus(InHole(WrongLength, _)), _, _)
    | ApPalette(InHole(WrongLength, _), _, _, _)
    | Subscript(InHole(WrongLength, _), _, _, _) => None
    | Case(InconsistentBranches(rule_types, _), scrut, rules) =>
      switch (syn(ctx, scrut)) {
      | None => None
      | Some(pat_ty) =>
        /* Make sure the rule synthesizes the type the rule_types says it does */
        let correct_rule_types =
          List.for_all2(
            (rule_ty, rule) => {
              switch (syn_rule(ctx, rule, pat_ty)) {
              | None => false
              | Some(syn_ty) => HTyp.eq(rule_ty, syn_ty)
              }
            },
            rule_types,
            rules,
          );
        if (correct_rule_types) {
          Some(HTyp.Hole);
        } else {
          None;
        };
      }
    /* not in hole */
    | Var(NotInHole, NotInVarHole, x) =>
      print_endline("Statics986");
      VarMap.lookup(Contexts.gamma(ctx), x);
    | Var(NotInHole, InVarHole(_), _) => Some(Hole)
    | IntLit(NotInHole, _) => Some(Int)
    | FloatLit(NotInHole, _) => Some(Float)
    | BoolLit(NotInHole, _) => Some(Bool)
    | StringLit(NotInHole, _) => Some(String)
    | ListNil(NotInHole) => Some(List(Hole))
    | Lam(NotInHole, p, ann, body) =>
      let ty1 =
        switch (ann) {
        | Some(uty) => UHTyp.expand(uty)
        | None => HTyp.Hole
        };
      switch (Pat.ana(ctx, p, ty1)) {
      | None => None
      | Some(ctx) =>
        switch (syn(ctx, body)) {
        | None => None
        | Some(ty2) => Some(HTyp.Arrow(ty1, ty2))
        }
      };
    | Inj(NotInHole, side, body) =>
      switch (syn(ctx, body)) {
      | None => None
      | Some(ty) =>
        switch (side) {
        | L => Some(Sum(ty, Hole))
        | R => Some(Sum(Hole, ty))
        }
      }
    | Case(StandardErrStatus(NotInHole), scrut, rules) =>
      switch (syn(ctx, scrut)) {
      | None => None
      | Some(b_ty) => syn_rules(ctx, rules, b_ty)
      }
    | ApPalette(NotInHole, name, serialized_model, psi) =>
      let palette_ctx = Contexts.palette_ctx(ctx);
      switch (PaletteCtx.lookup(palette_ctx, name)) {
      | None => None
      | Some(palette_defn) =>
        switch (ana_splice_map(ctx, SpliceInfo.splice_map(psi))) {
        | None => None
        | Some(splice_ctx) =>
          let expansion_ty = palette_defn.expansion_ty;
          let expand = palette_defn.expand;
          let expansion = expand(serialized_model);
          switch (ana(splice_ctx, expansion, expansion_ty)) {
          | None => None
          | Some(_) => Some(expansion_ty)
          };
        }
      };
    | Parenthesized(body) => syn(ctx, body)
    | Subscript(NotInHole, body1, body2, body3) =>
      switch (syn(ctx, body1)) {
      | None => None
      | Some(_) =>
        switch (ana(ctx, body2, Int), ana(ctx, body3, Int)) {
        | (Some(_), Some(_)) => Some(String)
        | (_, _) => None
        }
      }
    }
  and syn_rules =
      (ctx: Contexts.t, rules: UHExp.rules, pat_ty: HTyp.t): option(HTyp.t) => {
    let clause_types =
      List.fold_left(
        (types_opt, r) =>
          switch (types_opt) {
          | None => None
          | Some(types) =>
            switch (syn_rule(ctx, r, pat_ty)) {
            | None => None
            | Some(r_ty) => Some([r_ty, ...types])
            }
          },
        Some([]),
        rules,
      );
    switch (clause_types) {
    | None => None
    | Some(types) => HTyp.join_all(GLB, types)
    };
  }
  and syn_rule =
      (ctx: Contexts.t, rule: UHExp.rule, pat_ty: HTyp.t): option(HTyp.t) => {
    let Rule(p, clause) = rule;
    switch (Pat.ana(ctx, p, pat_ty)) {
    | None => None
    | Some(ctx) => syn(ctx, clause)
    };
  }
  and ana_splice_map =
      (ctx: Contexts.t, splice_map: UHExp.splice_map): option(Contexts.t) =>
    NatMap.fold(
      splice_map,
      (c, (splice_name, (ty, e))) =>
        switch (c) {
        | None => None
        | Some(splice_ctx) =>
          switch (ana(ctx, e, ty)) {
          | None => None
          | Some(_) =>
            let splice_var = SpliceInfo.var_of_splice_name(splice_name);
            Some(Contexts.extend_gamma(splice_ctx, (splice_var, ty)));
          }
        },
      Some(Contexts.empty),
    )
  /**
   * Analyze e against expected type ty
   */
  and ana = (ctx: Contexts.t, e: UHExp.t, ty: HTyp.t): option(unit) =>
    ana_block(ctx, e, ty)
  and ana_block =
      (ctx: Contexts.t, block: UHExp.block, ty: HTyp.t): option(unit) =>
    switch (block |> UHExp.Block.split_conclusion) {
    | None => None
    | Some((leading, conclusion)) =>
      switch (syn_lines(ctx, leading)) {
      | None => None
      | Some(ctx) => ana_opseq(ctx, conclusion, ty)
      }
    }
  and ana_opseq =
      (ctx: Contexts.t, OpSeq(skel, seq) as opseq: UHExp.opseq, ty: HTyp.t)
      : option(unit) =>
    switch (tuple_zip(skel, ty)) {
    | None =>
      switch (UHExp.get_err_status_opseq(opseq), HTyp.get_prod_elements(ty)) {
      | (InHole(TypeInconsistent, _), [_])
      | (InHole(WrongLength, _), _) =>
        let opseq' = opseq |> UHExp.set_err_status_opseq(NotInHole);
        syn_opseq(ctx, opseq') |> OptUtil.map(_ => ());
      | _ => None
      }
    | Some(skel_tys) =>
      skel_tys
      |> List.map(((skel, ty)) => ana_skel(ctx, skel, seq, ty))
      |> List.fold_left(OptUtil.map2((_, _) => ()), Some())
    }
  and ana_skel =
      (ctx: Contexts.t, skel: UHExp.skel, seq: UHExp.seq, ty: HTyp.t)
      : option(unit) =>
    switch (skel) {
    | BinOp(_, Comma, _, _)
    | BinOp(InHole(WrongLength, _), _, _, _) =>
      failwith(__LOC__ ++ ": tuples handled at opseq level")
    | Placeholder(n) =>
      let en = Seq.nth_operand(n, seq);
      ana_operand(ctx, en, ty);
    | BinOp(NotInHole, Cons, skel1, skel2) =>
      switch (HTyp.matched_list(ty)) {
      | None => None
      | Some(ty_elt) =>
        switch (ana_skel(ctx, skel1, seq, ty_elt)) {
        | None => None
        | Some(_) => ana_skel(ctx, skel2, seq, List(ty_elt))
        }
      }
    | BinOp(InHole(TypeInconsistent, _), _, _, _)
    | BinOp(
        NotInHole,
        And | Or | Minus | Plus | Times | Divide | FMinus | FPlus | FTimes |
        FDivide |
        LessThan |
        GreaterThan |
        Equals |
        FLessThan |
        FGreaterThan |
        FEquals |
        Space |
        Caret,
        _,
        _,
      ) =>
      switch (syn_skel(ctx, skel, seq)) {
      | None => None
      | Some(ty') => HTyp.consistent(ty, ty') ? Some() : None
      }
    }
  and ana_operand =
      (ctx: Contexts.t, operand: UHExp.operand, ty: HTyp.t): option(unit) =>
    switch (operand) {
    /* in hole */
    | EmptyHole(_) => Some()
    | Var(InHole(TypeInconsistent, _), _, _)
    | IntLit(InHole(TypeInconsistent, _), _)
    | FloatLit(InHole(TypeInconsistent, _), _)
    | BoolLit(InHole(TypeInconsistent, _), _)
    | StringLit(InHole(TypeInconsistent, _), _)
    | ListNil(InHole(TypeInconsistent, _))
    | Lam(InHole(TypeInconsistent, _), _, _, _)
    | Inj(InHole(TypeInconsistent, _), _, _)
    | Case(StandardErrStatus(InHole(TypeInconsistent, _)), _, _)
    | ApPalette(InHole(TypeInconsistent, _), _, _, _)
    | Subscript(InHole(TypeInconsistent, _), _, _, _) =>
      let operand' = UHExp.set_err_status_operand(NotInHole, operand);
      switch (syn_operand(ctx, operand')) {
      | None => None
      | Some(_) => Some() /* this is a consequence of subsumption and hole universality */
      };
    | Var(InHole(WrongLength, _), _, _)
    | IntLit(InHole(WrongLength, _), _)
    | FloatLit(InHole(WrongLength, _), _)
    | BoolLit(InHole(WrongLength, _), _)
    | StringLit(InHole(WrongLength, _), _)
    | ListNil(InHole(WrongLength, _))
    | Lam(InHole(WrongLength, _), _, _, _)
    | Inj(InHole(WrongLength, _), _, _)
    | Case(
        StandardErrStatus(InHole(WrongLength, _)) |
        InconsistentBranches(_, _),
        _,
        _,
      )
    | ApPalette(InHole(WrongLength, _), _, _, _)
    | Subscript(InHole(WrongLength, _), _, _, _) =>
      ty |> HTyp.get_prod_elements |> List.length > 1 ? Some() : None
    /* not in hole */
    | ListNil(NotInHole) =>
      switch (HTyp.matched_list(ty)) {
      | None => None
      | Some(_) => Some()
      }
    | Var(NotInHole, _, _)
    | IntLit(NotInHole, _)
    | FloatLit(NotInHole, _)
    | BoolLit(NotInHole, _)
    | StringLit(NotInHole, _) =>
      let operand' = UHExp.set_err_status_operand(NotInHole, operand);
      switch (syn_operand(ctx, operand')) {
      | None => None
      | Some(ty') =>
        if (HTyp.consistent(ty, ty')) {
          Some();
        } else {
          None;
        }
      };
    | Lam(NotInHole, p, ann, body) =>
      switch (HTyp.matched_arrow(ty)) {
      | None => None
      | Some((ty1_given, ty2)) =>
        switch (ann) {
        | Some(uty1) =>
          let ty1_ann = UHTyp.expand(uty1);
          switch (HTyp.consistent(ty1_ann, ty1_given)) {
          | false => None
          | true =>
            switch (Pat.ana(ctx, p, ty1_ann)) {
            | None => None
            | Some(ctx) => ana(ctx, body, ty2)
            }
          };
        | None =>
          switch (Pat.ana(ctx, p, ty1_given)) {
          | None => None
          | Some(ctx) => ana(ctx, body, ty2)
          }
        }
      }
    | Inj(NotInHole, side, body) =>
      switch (HTyp.matched_sum(ty)) {
      | None => None
      | Some((ty1, ty2)) => ana(ctx, body, InjSide.pick(side, ty1, ty2))
      }
    | Case(StandardErrStatus(NotInHole), scrut, rules) =>
      switch (syn(ctx, scrut)) {
      | None => None
      | Some(ty1) => ana_rules(ctx, rules, ty1, ty)
      }
    | ApPalette(NotInHole, _, _, _) =>
      switch (syn_operand(ctx, operand)) {
      | None => None
      | Some(ty') =>
        if (HTyp.consistent(ty, ty')) {
          Some();
        } else {
          None;
        }
      }
    | Parenthesized(body) => ana(ctx, body, ty)
    | Subscript(NotInHole, _, _, _) =>
      switch (syn_operand(ctx, operand)) {
      | None => None
      | Some(_) => Some()
      }
    }
  and ana_rules =
      (ctx: Contexts.t, rules: UHExp.rules, pat_ty: HTyp.t, clause_ty: HTyp.t)
      : option(unit) =>
    List.fold_left(
      (b, r) =>
        switch (b) {
        | None => None
        | Some(_) => ana_rule(ctx, r, pat_ty, clause_ty)
        },
      Some(),
      rules,
    )
  and ana_rule =
      (
        ctx: Contexts.t,
        Rule(p, clause): UHExp.rule,
        pat_ty: HTyp.t,
        clause_ty: HTyp.t,
      )
      : option(unit) =>
    switch (Pat.ana(ctx, p, pat_ty)) {
    | None => None
    | Some(ctx) => ana(ctx, clause, clause_ty)
    };

  /**
   * Get type mode of nth operand of an opseq in synthetic position
   */
  let rec syn_nth_type_mode =
          (ctx: Contexts.t, n: int, OpSeq(skel, seq): UHExp.opseq)
          : option(type_mode) =>
    syn_nth_type_mode'(ctx, n, skel, seq)
  and syn_nth_type_mode' =
      (ctx: Contexts.t, n: int, skel: UHExp.skel, seq: UHExp.seq)
      : option(type_mode) => {
    let ana_go = (skel, ty) => ana_nth_type_mode'(ctx, n, skel, seq, ty);
    let rec go = (skel: UHExp.skel) =>
      switch (skel) {
      | Placeholder(n') =>
        assert(n == n');
        Some(Syn);
      | BinOp(InHole(_), op, skel1, skel2) =>
        go(BinOp(NotInHole, op, skel1, skel2))
      | BinOp(NotInHole, Comma, skel1, skel2) =>
        n <= Skel.rightmost_tm_index(skel1) ? go(skel1) : go(skel2)
      | BinOp(NotInHole, Space, skel1, skel2) =>
        switch (syn_skel(ctx, skel1, seq)) {
        | None => None
        | Some(ty1) =>
          if (n <= Skel.rightmost_tm_index(skel1)) {
            go(skel1);
          } else {
            switch (HTyp.matched_arrow(ty1)) {
            | None => None
            | Some((ty2, _)) => ana_go(skel2, ty2)
            };
          }
        }
      | BinOp(NotInHole, Cons, skel1, skel2) =>
        switch (syn_skel(ctx, skel1, seq)) {
        | None => None
        | Some(ty1) =>
          n <= Skel.rightmost_tm_index(skel1)
            ? go(skel1) : ana_go(skel2, HTyp.List(ty1))
        }
      | BinOp(
          NotInHole,
          Plus | Minus | Times | Divide | LessThan | GreaterThan,
          skel1,
          skel2,
        ) =>
        n <= Skel.rightmost_tm_index(skel1)
          ? ana_go(skel1, Int) : ana_go(skel2, Int)
      | BinOp(
          NotInHole,
          FPlus | FMinus | FTimes | FDivide | FLessThan | FGreaterThan,
          skel1,
          skel2,
        ) =>
        n <= Skel.rightmost_tm_index(skel1)
          ? ana_go(skel1, Float) : ana_go(skel2, Float)
      | BinOp(NotInHole, And | Or, skel1, skel2) =>
        n <= Skel.rightmost_tm_index(skel1)
          ? ana_go(skel1, Bool) : ana_go(skel2, Bool)
      | BinOp(NotInHole, Caret, skel1, skel2) =>
        n <= Skel.rightmost_tm_index(skel1)
          ? ana_go(skel1, String) : ana_go(skel2, String)
      | BinOp(NotInHole, Equals, skel1, skel2) =>
        if (n <= Skel.rightmost_tm_index(skel1)) {
          go(skel1);
        } else {
          switch (syn_skel(ctx, skel1, seq)) {
          | None => None
          | Some(ty1) => ana_go(skel2, ty1)
          };
        }
      | BinOp(NotInHole, FEquals, skel1, skel2) =>
        if (n <= Skel.rightmost_tm_index(skel1)) {
          go(skel1);
        } else {
          switch (syn_skel(ctx, skel1, seq)) {
          | None => None
          | Some(ty1) => ana_go(skel2, ty1)
          };
        }
      };
    go(skel);
  }
  /**
   * Get type mode of nth operand of an opseq in analytic position
   */
  and ana_nth_type_mode =
      (
        ctx: Contexts.t,
        n: int,
        OpSeq(skel, seq) as opseq: UHExp.opseq,
        ty: HTyp.t,
      )
      : option(type_mode) => {
    // handle n-tuples
    switch (tuple_zip(skel, ty)) {
    | None =>
      syn_nth_type_mode(
        ctx,
        n,
        opseq |> UHExp.set_err_status_opseq(NotInHole),
      )
    | Some(skel_tys) =>
      let (nskel, nty) =
        skel_tys
        |> List.find(((skel, _)) =>
             Skel.leftmost_tm_index(skel) <= n
             && n <= Skel.rightmost_tm_index(skel)
           );
      ana_nth_type_mode'(ctx, n, nskel, seq, nty);
    };
  }
  and ana_nth_type_mode' =
      (ctx: Contexts.t, n: int, skel: UHExp.skel, seq: UHExp.seq, ty: HTyp.t)
      : option(type_mode) => {
    let syn_go = skel => syn_nth_type_mode'(ctx, n, skel, seq);
    let rec go = (skel: UHExp.skel, ty: HTyp.t) =>
      switch (skel) {
      | BinOp(_, Comma, _, _)
      | BinOp(InHole(WrongLength, _), _, _, _) =>
        failwith(__LOC__ ++ ": expected tuples to be handled at opseq level")
      | Placeholder(n') =>
        assert(n == n');
        Some(Ana(ty));
      | BinOp(InHole(TypeInconsistent, _), op, skel1, skel2) =>
        let skel_not_in_hole = Skel.BinOp(NotInHole, op, skel1, skel2);
        syn_go(skel_not_in_hole);
      | BinOp(NotInHole, Cons, skel1, skel2) =>
        switch (HTyp.matched_list(ty)) {
        | None => None
        | Some(ty_elt) =>
          n <= Skel.rightmost_tm_index(skel1)
            ? go(skel1, ty_elt) : go(skel2, ty)
        }
      | BinOp(
          NotInHole,
          And | Or | Minus | Plus | Times | Divide | FMinus | FPlus | FTimes |
          FDivide |
          LessThan |
          GreaterThan |
          Equals |
          FLessThan |
          FGreaterThan |
          FEquals |
          Space |
          Caret,
          _,
          _,
        ) =>
        syn_go(skel)
      };
    go(skel, ty);
  };

  /* If renumber_empty_holes is true, then the metavars in empty holes will be assigned
   * new values in the same namespace as non-empty holes. Non-empty holes are renumbered
   * regardless.
   */
  let rec syn_fix_holes =
          (
            ctx: Contexts.t,
            u_gen: MetaVarGen.t,
            ~renumber_empty_holes=false,
            e: UHExp.t,
          )
          : (UHExp.t, HTyp.t, MetaVarGen.t) =>
    syn_fix_holes_block(ctx, u_gen, ~renumber_empty_holes, e)
  and syn_fix_holes_block =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        block: UHExp.block,
      )
      : (UHExp.block, HTyp.t, MetaVarGen.t) =>
    switch (block |> UHExp.Block.split_conclusion) {
    | None =>
      let (leading, _ctx, u_gen) =
        syn_fix_holes_lines(ctx, u_gen, ~renumber_empty_holes, block);
      let (conclusion, u_gen) = u_gen |> UHExp.new_EmptyHole;
      (leading @ [UHExp.ExpLine(conclusion |> OpSeq.wrap)], Hole, u_gen);
    | Some((leading, conclusion)) =>
      let (leading, ctx, u_gen) =
        syn_fix_holes_lines(ctx, u_gen, ~renumber_empty_holes, leading);
      let (conclusion, ty, u_gen) =
        syn_fix_holes_opseq(ctx, u_gen, ~renumber_empty_holes, conclusion);
      (leading @ [UHExp.ExpLine(conclusion)], ty, u_gen);
    }
  and syn_fix_holes_lines =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        lines: list(UHExp.line),
      )
      : (list(UHExp.line), Contexts.t, MetaVarGen.t) => {
    let (rev_fixed_lines, ctx, u_gen) =
      lines
      |> List.fold_left(
           (
             (fixed_lines, ctx, u_gen): (
               list(UHExp.line),
               Contexts.t,
               MetaVarGen.t,
             ),
             line: UHExp.line,
           ) => {
             let (fixed_line, ctx, u_gen) =
               syn_fix_holes_line(ctx, u_gen, ~renumber_empty_holes, line);
             ([fixed_line, ...fixed_lines], ctx, u_gen);
           },
           ([], ctx, u_gen),
         );
    (rev_fixed_lines |> List.rev, ctx, u_gen);
  }
  and syn_fix_holes_line =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        line: UHExp.line,
      )
      : (UHExp.line, Contexts.t, MetaVarGen.t) =>
    switch (line) {
    | ExpLine(e) =>
      let (e, _, u_gen) =
        syn_fix_holes_opseq(ctx, u_gen, ~renumber_empty_holes, e);
      (ExpLine(e), ctx, u_gen);
    | EmptyLine => (line, ctx, u_gen)
    | LetLine(p, ann, def) =>
      switch (ann) {
      | Some(uty1) =>
        let ty1 = UHTyp.expand(uty1);
        let ctx_def = ctx_for_let(ctx, p, ty1, def);
        let (def, u_gen) =
          ana_fix_holes(ctx_def, u_gen, ~renumber_empty_holes, def, ty1);
        let (p, ctx, u_gen) =
          Pat.ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p, ty1);
        (LetLine(p, ann, def), ctx, u_gen);
      | None =>
        let (def, ty1, u_gen) =
          syn_fix_holes(~renumber_empty_holes, ctx, u_gen, def);
        let (p, ctx, u_gen) =
          Pat.ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p, ty1);
        (LetLine(p, ann, def), ctx, u_gen);
      }
    }
  and syn_fix_holes_opseq =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        OpSeq(skel, seq): UHExp.opseq,
      )
      : (UHExp.opseq, HTyp.t, MetaVarGen.t) => {
    let (skel, seq, ty, u_gen) =
      syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel, seq);
    (OpSeq(skel, seq), ty, u_gen);
  }
  and syn_fix_holes_skel =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        skel: UHExp.skel,
        seq: UHExp.seq,
      )
      : (UHExp.skel, UHExp.seq, HTyp.t, MetaVarGen.t) =>
    switch (skel) {
    | Placeholder(n) =>
      let en = seq |> Seq.nth_operand(n);
      let (en, ty, u_gen) =
        syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, en);
      let seq = seq |> Seq.update_nth_operand(n, en);
      (skel, seq, ty, u_gen);
    | BinOp(_, (Minus | Plus | Times | Divide) as op, skel1, skel2) =>
      let (skel1, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Int,
        );
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Int,
        );
      (BinOp(NotInHole, op, skel1, skel2), seq, Int, u_gen);
    | BinOp(_, (FMinus | FPlus | FTimes | FDivide) as op, skel1, skel2) =>
      let (skel1, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Float,
        );
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Float,
        );
      (BinOp(NotInHole, op, skel1, skel2), seq, Float, u_gen);
    | BinOp(_, (And | Or) as op, skel1, skel2) =>
      let (skel1, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Bool,
        );
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Bool,
        );
      (BinOp(NotInHole, op, skel1, skel2), seq, Bool, u_gen);
    | BinOp(_, Caret as op, skel1, skel2) =>
      let (skel1, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.String,
        );
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.String,
        );
      (BinOp(NotInHole, op, skel1, skel2), seq, String, u_gen);
    | BinOp(_, (LessThan | GreaterThan | Equals) as op, skel1, skel2) =>
      let (skel1, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Int,
        );
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Int,
        );
      (BinOp(NotInHole, op, skel1, skel2), seq, Bool, u_gen);
    | BinOp(_, (FLessThan | FGreaterThan | FEquals) as op, skel1, skel2) =>
      let (skel1, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel1,
          seq,
          HTyp.Float,
        );
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          skel2,
          seq,
          HTyp.Float,
        );
      (BinOp(NotInHole, op, skel1, skel2), seq, Bool, u_gen);
    | BinOp(_, Space, skel1, skel2) =>
      let (skel1, seq, ty1, u_gen) =
        syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel1, seq);
      switch (HTyp.matched_arrow(ty1)) {
      | Some((ty2, ty)) =>
        let (skel2, seq, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel2,
            seq,
            ty2,
          );
        (BinOp(NotInHole, Space, skel1, skel2), seq, ty, u_gen);
      | None =>
        let (skel2, seq, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel2,
            seq,
            HTyp.Hole,
          );
        let (OpSeq(skel1, seq), u_gen) =
          UHExp.mk_inconsistent_opseq(u_gen, OpSeq(skel1, seq));
        (BinOp(NotInHole, Space, skel1, skel2), seq, Hole, u_gen);
      };
    | BinOp(_, Comma, _, _) =>
      let ((u_gen, seq), pairs) =
        skel
        |> UHExp.get_tuple_elements
        |> ListUtil.map_with_accumulator(
             ((u_gen, seq), skel) => {
               let (skel, seq, ty, u_gen) =
                 syn_fix_holes_skel(
                   ctx,
                   u_gen,
                   ~renumber_empty_holes,
                   skel,
                   seq,
                 );
               ((u_gen, seq), (skel, ty));
             },
             (u_gen, seq),
           );
      let (skels, tys) = List.split(pairs);
      (UHExp.mk_tuple(skels), seq, Prod(tys), u_gen);
    | BinOp(_, Cons, skel1, skel2) =>
      let (skel1, seq, ty_elt, u_gen) =
        syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel1, seq);
      let ty = HTyp.List(ty_elt);
      let (skel2, seq, u_gen) =
        ana_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel2, seq, ty);
      let skel = Skel.BinOp(NotInHole, Operators.Exp.Cons, skel1, skel2);
      (skel, seq, ty, u_gen);
    }
  and syn_fix_holes_operand =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        e: UHExp.operand,
      )
      : (UHExp.operand, HTyp.t, MetaVarGen.t) => {
    let e_nih = UHExp.set_err_status_operand(NotInHole, e);
    switch (e) {
    | EmptyHole(_) =>
      if (renumber_empty_holes) {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (EmptyHole(u), Hole, u_gen);
      } else {
        (e, Hole, u_gen);
      }
    | Var(_, var_err_status, x) =>
      let gamma = Contexts.gamma(ctx);
      switch (VarMap.lookup(gamma, x)) {
      | Some(ty) => (UHExp.Var(NotInHole, NotInVarHole, x), ty, u_gen)
      | None =>
        switch (var_err_status) {
        | InVarHole(_, _) => (e_nih, HTyp.Hole, u_gen)
        | NotInVarHole =>
          let (u, u_gen) = MetaVarGen.next(u_gen);
          let reason: VarErrStatus.HoleReason.t =
            switch (Var.is_let(x), Var.is_case(x)) {
            | (true, _) => Keyword(Let)
            | (_, true) => Keyword(Case)
            | _ => Free
            };
          (Var(NotInHole, InVarHole(reason, u), x), Hole, u_gen);
        }
      };
    | IntLit(_, _) => (e_nih, Int, u_gen)
    | FloatLit(_, _) => (e_nih, Float, u_gen)
    | BoolLit(_, _) => (e_nih, Bool, u_gen)
    | StringLit(_, _) => (e_nih, String, u_gen)
    | ListNil(_) => (e_nih, List(Hole), u_gen)
    | Parenthesized(body) =>
      let (block, ty, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, body);
      (Parenthesized(block), ty, u_gen);
    | Lam(_, p, ann, body) =>
      let ty1 =
        switch (ann) {
        | Some(uty1) => UHTyp.expand(uty1)
        | None => HTyp.Hole
        };
      let (p, ctx, u_gen) =
        Pat.ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p, ty1);
      let (body, ty2, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, body);
      (Lam(NotInHole, p, ann, body), Arrow(ty1, ty2), u_gen);
    | Inj(_, side, body) =>
      let (body, ty1, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, body);
      let ty =
        switch (side) {
        | L => HTyp.Sum(ty1, Hole)
        | R => HTyp.Sum(Hole, ty1)
        };
      (Inj(NotInHole, side, body), ty, u_gen);
    | Case(_, scrut, rules) =>
      let (scrut, ty1, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, scrut);
      let (rules, u_gen, rule_types, common_type) =
        syn_fix_holes_rules(ctx, u_gen, ~renumber_empty_holes, rules, ty1);
      switch (common_type) {
      | None =>
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (
          Case(InconsistentBranches(rule_types, u), scrut, rules),
          HTyp.Hole,
          u_gen,
        );
      | Some(common_type) => (
          Case(StandardErrStatus(NotInHole), scrut, rules),
          common_type,
          u_gen,
        )
      };
    | ApPalette(_, name, serialized_model, psi) =>
      let palette_ctx = Contexts.palette_ctx(ctx);
      switch (PaletteCtx.lookup(palette_ctx, name)) {
      | None => raise(PaletteCtx.InvalidPaletteHoleName) /* TODO invalid palette name hole */
      | Some(palette_defn) =>
        let (splice_map, u_gen) =
          ana_fix_holes_splice_map(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            SpliceInfo.splice_map(psi),
          );
        let psi = SpliceInfo.update_splice_map(psi, splice_map);
        let expansion_ty = palette_defn.expansion_ty;
        (
          ApPalette(NotInHole, name, serialized_model, psi),
          expansion_ty,
          u_gen,
        );
      };
    | Subscript(_, body1, body2, body3) =>
      let (body1, u_gen) =
        ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, body1, String);
      let (body2, u_gen) =
        ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, body2, Int);
      let (body3, u_gen) =
        ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, body3, Int);
      (Subscript(NotInHole, body1, body2, body3), String, u_gen);
    };
  }
  and syn_fix_holes_rules =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        rules: UHExp.rules,
        pat_ty: HTyp.t,
      )
      : (UHExp.rules, MetaVarGen.t, list(HTyp.t), option(HTyp.t)) => {
    let (rev_fixed_rules, u_gen, rule_types) =
      List.fold_left(
        ((rules, u_gen, rule_types), r) => {
          let (r, u_gen, r_ty) =
            syn_fix_holes_rule(ctx, u_gen, ~renumber_empty_holes, r, pat_ty);
          ([r, ...rules], u_gen, [r_ty, ...rule_types]);
        },
        ([], u_gen, []),
        rules,
      );
    let common_type = HTyp.join_all(GLB, rule_types);
    (List.rev(rev_fixed_rules), u_gen, List.rev(rule_types), common_type);
  }
  and syn_fix_holes_rule =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        rule: UHExp.rule,
        pat_ty: HTyp.t,
      )
      : (UHExp.rule, MetaVarGen.t, HTyp.t) => {
    let Rule(p, clause) = rule;
    let (p, ctx, u_gen) =
      Pat.ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p, pat_ty);
    let (clause, clause_ty, u_gen) =
      syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, clause);
    (Rule(p, clause), u_gen, clause_ty);
  }
  and ana_fix_holes_rules =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        rules: UHExp.rules,
        pat_ty: HTyp.t,
        clause_ty: HTyp.t,
      )
      : (UHExp.rules, MetaVarGen.t) => {
    let (rev_fixed_rules, u_gen) =
      List.fold_left(
        ((rules, u_gen), r) => {
          let (r, u_gen) =
            ana_fix_holes_rule(
              ctx,
              u_gen,
              ~renumber_empty_holes,
              r,
              pat_ty,
              clause_ty,
            );
          ([r, ...rules], u_gen);
        },
        ([], u_gen),
        rules,
      );
    (List.rev(rev_fixed_rules), u_gen);
  }
  and ana_fix_holes_rule =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        Rule(p, clause): UHExp.rule,
        pat_ty: HTyp.t,
        clause_ty: HTyp.t,
      )
      : (UHExp.rule, MetaVarGen.t) => {
    let (p, ctx, u_gen) =
      Pat.ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, p, pat_ty);
    let (clause, u_gen) =
      ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, clause, clause_ty);
    (Rule(p, clause), u_gen);
  }
  and ana_fix_holes_splice_map =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        splice_map: UHExp.splice_map,
      )
      : (UHExp.splice_map, MetaVarGen.t) =>
    NatMap.fold(
      splice_map,
      ((splice_map, u_gen), (splice_name, (ty, e))) => {
        let (e, u_gen) =
          ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, e, ty);
        let splice_map =
          NatMap.extend_unique(splice_map, (splice_name, (ty, e)));
        (splice_map, u_gen);
      },
      (splice_map, u_gen),
    )
  and ana_fix_holes =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        e: UHExp.t,
        ty: HTyp.t,
      )
      : (UHExp.t, MetaVarGen.t) =>
    ana_fix_holes_block(ctx, u_gen, ~renumber_empty_holes, e, ty)
  and ana_fix_holes_block =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        block: UHExp.block,
        ty: HTyp.t,
      )
      : (UHExp.block, MetaVarGen.t) =>
    switch (block |> UHExp.Block.split_conclusion) {
    | None =>
      let (leading, _ctx, u_gen) =
        syn_fix_holes_lines(ctx, u_gen, ~renumber_empty_holes, block);
      let (conclusion, u_gen) = u_gen |> UHExp.new_EmptyHole;
      (leading @ [UHExp.ExpLine(conclusion |> OpSeq.wrap)], u_gen);
    | Some((leading, conclusion)) =>
      let (leading, ctx, u_gen) =
        syn_fix_holes_lines(ctx, u_gen, ~renumber_empty_holes, leading);
      let (conclusion, u_gen) =
        ana_fix_holes_opseq(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          conclusion,
          ty,
        );
      (leading @ [UHExp.ExpLine(conclusion)], u_gen);
    }
  and ana_fix_holes_opseq =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        OpSeq(skel, seq) as opseq: UHExp.opseq,
        ty: HTyp.t,
      )
      : (UHExp.opseq, MetaVarGen.t) => {
    // handle n-tuples
    switch (tuple_zip(skel, ty)) {
    | Some(skel_tys) =>
      skel_tys
      |> List.fold_left(
           (
             (
               rev_skels: list(UHExp.skel),
               seq: UHExp.seq,
               u_gen: MetaVarGen.t,
             ),
             (skel: UHExp.skel, ty: HTyp.t),
           ) => {
             let (skel, seq, u_gen) =
               ana_fix_holes_skel(
                 ctx,
                 u_gen,
                 ~renumber_empty_holes,
                 skel,
                 seq,
                 ty,
               );
             ([skel, ...rev_skels], seq, u_gen);
           },
           ([], seq, u_gen),
         )
      |> (
        fun
        | (rev_skels, seq, u_gen) => {
            let skel = rev_skels |> List.rev |> UHExp.mk_tuple;
            (OpSeq.OpSeq(skel, seq), u_gen);
          }
      )
    | None =>
      if (List.length(HTyp.get_prod_elements(ty)) == 1) {
        skel
        |> UHExp.get_tuple_elements
        |> List.fold_left(
             (
               (
                 rev_skels: list(UHExp.skel),
                 seq: UHExp.seq,
                 u_gen: MetaVarGen.t,
               ),
               skel: UHExp.skel,
             ) => {
               let (skel, seq, _, u_gen) =
                 syn_fix_holes_skel(
                   ctx,
                   u_gen,
                   ~renumber_empty_holes,
                   skel,
                   seq,
                 );
               ([skel, ...rev_skels], seq, u_gen);
             },
             ([], seq, u_gen),
           )
        |> (
          fun
          | (rev_skels, seq, u_gen) => {
              let (u, u_gen) = MetaVarGen.next(u_gen);
              let skel = UHExp.mk_tuple(List.rev(rev_skels));
              let opseq =
                UHExp.set_err_status_opseq(
                  InHole(TypeInconsistent, u),
                  OpSeq.OpSeq(skel, seq),
                );
              (opseq, u_gen);
            }
        );
      } else {
        let (u, u_gen) = u_gen |> MetaVarGen.next;
        let (opseq, _, u_gen) =
          syn_fix_holes_opseq(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            opseq |> UHExp.set_err_status_opseq(NotInHole),
          );
        (
          opseq |> UHExp.set_err_status_opseq(InHole(WrongLength, u)),
          u_gen,
        );
      }
    };
  }
  and ana_fix_holes_skel =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        skel: UHExp.skel,
        seq: UHExp.seq,
        ty: HTyp.t,
      )
      : (UHExp.skel, UHExp.seq, MetaVarGen.t) =>
    switch (skel) {
    | BinOp(_, Comma, _, _) =>
      failwith("Exp.ana_fix_holes_skel: tuples handled at opseq level")
    | Placeholder(n) =>
      let en = seq |> Seq.nth_operand(n);
      let (en, u_gen) =
        ana_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, en, ty);
      let seq = seq |> Seq.update_nth_operand(n, en);
      (skel, seq, u_gen);
    | BinOp(_, Cons, skel1, skel2) =>
      switch (HTyp.matched_list(ty)) {
      | Some(ty_elt) =>
        let (skel1, seq, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel1,
            seq,
            ty_elt,
          );
        let ty_list = HTyp.List(ty_elt);
        let (skel2, seq, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel2,
            seq,
            ty_list,
          );
        let skel = Skel.BinOp(NotInHole, Operators.Exp.Cons, skel1, skel2);
        (skel, seq, u_gen);
      | None =>
        let (skel1, seq, ty_elt, u_gen) =
          syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel1, seq);
        let ty_list = HTyp.List(ty_elt);
        let (skel2, seq, u_gen) =
          ana_fix_holes_skel(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            skel2,
            seq,
            ty_list,
          );
        let (u, u_gen) = MetaVarGen.next(u_gen);
        let skel =
          Skel.BinOp(
            InHole(TypeInconsistent, u),
            Operators.Exp.Cons,
            skel1,
            skel2,
          );
        (skel, seq, u_gen);
      }
    | BinOp(
        _,
        And | Or | Minus | Plus | Times | Divide | FMinus | FPlus | FTimes |
        FDivide |
        LessThan |
        GreaterThan |
        Equals |
        FLessThan |
        FGreaterThan |
        FEquals |
        Space |
        Caret,
        _,
        _,
      ) =>
      let (skel, seq, ty', u_gen) =
        syn_fix_holes_skel(ctx, u_gen, ~renumber_empty_holes, skel, seq);
      if (HTyp.consistent(ty, ty')) {
        (skel, seq, u_gen);
      } else {
        let (OpSeq(skel, seq), u_gen) =
          UHExp.mk_inconsistent_opseq(u_gen, OpSeq(skel, seq));
        (skel, seq, u_gen);
      };
    }
  and ana_fix_holes_operand =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        ~renumber_empty_holes=false,
        e: UHExp.operand,
        ty: HTyp.t,
      )
      : (UHExp.operand, MetaVarGen.t) =>
    switch (e) {
    | EmptyHole(_) =>
      if (renumber_empty_holes) {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (EmptyHole(u), u_gen);
      } else {
        (e, u_gen);
      }
    | Var(_, _, _)
    | IntLit(_, _)
    | FloatLit(_, _)
    | BoolLit(_, _)
    | StringLit(_, _) =>
      let (e, ty', u_gen) =
        syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, e);
      if (HTyp.consistent(ty, ty')) {
        (UHExp.set_err_status_operand(NotInHole, e), u_gen);
      } else {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (
          UHExp.set_err_status_operand(InHole(TypeInconsistent, u), e),
          u_gen,
        );
      };
    | ListNil(_) =>
      switch (HTyp.matched_list(ty)) {
      | Some(_) => (UHExp.set_err_status_operand(NotInHole, e), u_gen)
      | None =>
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (ListNil(InHole(TypeInconsistent, u)), u_gen);
      }
    | Parenthesized(body) =>
      let (body, u_gen) =
        ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, body, ty);
      (Parenthesized(body), u_gen);
    | Lam(_, p, ann, def) =>
      switch (HTyp.matched_arrow(ty)) {
      | Some((ty1_given, ty2)) =>
        switch (ann) {
        | Some(uty1) =>
          let ty1_ann = UHTyp.expand(uty1);
          if (HTyp.consistent(ty1_ann, ty1_given)) {
            let (p, ctx, u_gen) =
              Pat.ana_fix_holes(
                ctx,
                u_gen,
                ~renumber_empty_holes,
                p,
                ty1_ann,
              );
            let (def, u_gen) =
              ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, def, ty2);
            (UHExp.Lam(NotInHole, p, ann, def), u_gen);
          } else {
            let (e', _, u_gen) =
              syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, e);
            let (u, u_gen) = MetaVarGen.next(u_gen);
            (
              UHExp.set_err_status_operand(InHole(TypeInconsistent, u), e'),
              u_gen,
            );
          };
        | None =>
          let (p, ctx, u_gen) =
            Pat.ana_fix_holes(
              ctx,
              u_gen,
              ~renumber_empty_holes,
              p,
              ty1_given,
            );
          let (def, u_gen) =
            ana_fix_holes(ctx, u_gen, ~renumber_empty_holes, def, ty2);
          (UHExp.Lam(NotInHole, p, ann, def), u_gen);
        }
      | None =>
        let (e', _, u_gen) =
          syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, e);
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (
          UHExp.set_err_status_operand(InHole(TypeInconsistent, u), e'),
          u_gen,
        );
      }
    | Inj(_, side, body) =>
      switch (HTyp.matched_sum(ty)) {
      | Some((ty1, ty2)) =>
        let (e1, u_gen) =
          ana_fix_holes(
            ctx,
            u_gen,
            ~renumber_empty_holes,
            body,
            InjSide.pick(side, ty1, ty2),
          );
        (Inj(NotInHole, side, e1), u_gen);
      | None =>
        let (e', ty', u_gen) =
          syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, e);
        if (HTyp.consistent(ty, ty')) {
          (UHExp.set_err_status_operand(NotInHole, e'), u_gen);
        } else {
          let (u, u_gen) = MetaVarGen.next(u_gen);
          (
            UHExp.set_err_status_operand(InHole(TypeInconsistent, u), e'),
            u_gen,
          );
        };
      }
    | Case(_, scrut, rules) =>
      let (scrut, scrut_ty, u_gen) =
        syn_fix_holes(ctx, u_gen, ~renumber_empty_holes, scrut);
      let (rules, u_gen) =
        ana_fix_holes_rules(
          ctx,
          u_gen,
          ~renumber_empty_holes,
          rules,
          scrut_ty,
          ty,
        );
      (Case(StandardErrStatus(NotInHole), scrut, rules), u_gen);
    | ApPalette(_, _, _, _) =>
      let (e', ty', u_gen) =
        syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, e);
      if (HTyp.consistent(ty, ty')) {
        (UHExp.set_err_status_operand(NotInHole, e'), u_gen);
      } else {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (
          UHExp.set_err_status_operand(InHole(TypeInconsistent, u), e'),
          u_gen,
        );
      };
    | Subscript(_, _, _, _) =>
      let (e', ty', u_gen) =
        syn_fix_holes_operand(ctx, u_gen, ~renumber_empty_holes, e);
      if (HTyp.consistent(ty, ty')) {
        (UHExp.set_err_status_operand(NotInHole, e'), u_gen);
      } else {
        let (u, u_gen) = MetaVarGen.next(u_gen);
        (
          UHExp.set_err_status_operand(InHole(TypeInconsistent, u), e'),
          u_gen,
        );
      };
    };

  let syn_fix_holes_z =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, ze: ZExp.t)
      : (ZExp.t, HTyp.t, MetaVarGen.t) => {
    let path = CursorPath.Exp.of_z(ze);
    let (e, ty, u_gen) = syn_fix_holes(ctx, u_gen, ZExp.erase(ze));
    let ze =
      CursorPath.Exp.follow(path, e)
      |> OptUtil.get(() =>
           failwith(
             "syn_fix_holes did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (ze, ty, u_gen);
  };

  let syn_fix_holes_zlines =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, zlines: ZExp.zblock)
      : (ZExp.zblock, Contexts.t, MetaVarGen.t) => {
    let path = CursorPath.Exp.of_zblock(zlines);
    let (lines, ctx, u_gen) =
      syn_fix_holes_lines(ctx, u_gen, ZExp.erase_zblock(zlines));
    let zlines =
      CursorPath.Exp.follow_block(path, lines)
      |> OptUtil.get(() =>
           failwith(
             "syn_fix_holes_lines did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (zlines, ctx, u_gen);
  };

  let syn_fix_holes_zrules =
      (
        ctx: Contexts.t,
        u_gen: MetaVarGen.t,
        zrules: ZExp.zrules,
        pat_ty: HTyp.t,
      )
      : (ZExp.zrules, list(HTyp.t), option(HTyp.t), MetaVarGen.t) => {
    let path = CursorPath.Exp.of_zrules(zrules);
    let rules = ZExp.erase_zrules(zrules);
    let (rules, u_gen, rule_types, common_type) =
      syn_fix_holes_rules(ctx, u_gen, rules, pat_ty);
    let zrules =
      CursorPath.Exp.follow_rules(path, rules)
      |> OptUtil.get(() =>
           failwith(
             "syn_fix_holes_rules did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (zrules, rule_types, common_type, u_gen);
  };

  let ana_fix_holes_z =
      (ctx: Contexts.t, u_gen: MetaVarGen.t, ze: ZExp.t, ty: HTyp.t)
      : (ZExp.t, MetaVarGen.t) => {
    let path = CursorPath.Exp.of_z(ze);
    let (e, u_gen) = ana_fix_holes(ctx, u_gen, ZExp.erase(ze), ty);
    let ze =
      CursorPath.Exp.follow(path, e)
      |> OptUtil.get(() =>
           failwith(
             "ana_fix_holes did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (ze, u_gen);
  };

  /* Only to be used on top-level expressions, as it starts hole renumbering at 0 */
  let fix_and_renumber_holes =
      (ctx: Contexts.t, e: UHExp.t): (UHExp.t, HTyp.t, MetaVarGen.t) =>
    syn_fix_holes(ctx, MetaVarGen.init, ~renumber_empty_holes=true, e);

  let fix_and_renumber_holes_z = (ctx: Contexts.t, ze: ZExp.t): edit_state => {
    let path = CursorPath.Exp.of_z(ze);
    let (e, ty, u_gen) = fix_and_renumber_holes(ctx, ZExp.erase(ze));
    let ze =
      CursorPath.Exp.follow(path, e)
      |> OptUtil.get(() =>
           failwith(
             "fix_and_renumber_holes did not preserve path "
             ++ Sexplib.Sexp.to_string(CursorPath.sexp_of_t(path)),
           )
         );
    (ze, ty, u_gen);
  };
};