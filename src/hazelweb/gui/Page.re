module Js = Js_of_ocaml.Js;
module Vdom = Virtual_dom.Vdom;

let examples_select = (~inject: ModelAction.t => Vdom.Event.t) =>
  Vdom.(
    Node.select(
      [
        Attr.on_change((_, example_id) =>
          inject(ModelAction.LoadExample(example_id))
        ),
      ],
      [
        Node.option([Attr.value("just_hole")], [Node.text("just a hole")]),
        Node.option(
          [Attr.value("holey_lambda")],
          [Node.text("holey lambda")],
        ),
        Node.option(
          [Attr.value("let_line")],
          [Node.text("let with extra lines")],
        ),
        Node.option([Attr.value("map_example")], [Node.text("map")]),
        Node.option([Attr.value("qsort_example")], [Node.text("qsort")]),
        Node.option(
          [Attr.value("qsort_example_3")],
          [Node.text("qsort (3x)")],
        ),
        Node.option(
          [Attr.value("qsort_example_10")],
          [Node.text("qsort (10x)")],
        ),
        Node.option(
          [Attr.value("qsort_example_30")],
          [Node.text("qsort (30x)")],
        ),
        Node.option(
          [Attr.value("qsort_example_100")],
          [Node.text("qsort (100x)")],
        ),
        Node.option([Attr.value("local")], [Node.text("local")]),
      ],
    )
  );

let cardstacks_select =
    (
      ~inject: ModelAction.t => Vdom.Event.t,
      cardstacks: list(CardstackInfo.t),
    ) => {
  let cardstack_options =
    List.mapi(
      (i, cardstack: CardstackInfo.t) => {
        let example_idx = string_of_int(i);
        Vdom.(
          Node.option(
            [Attr.value(example_idx)],
            [Node.text(cardstack.title)],
          )
        );
      },
      cardstacks,
    );
  Vdom.(
    Node.select(
      [
        Attr.on_change((_, example_idx) =>
          inject(ModelAction.LoadCardstack(int_of_string(example_idx)))
        ),
      ],
      cardstack_options,
    )
  );
};

let prev_card_button = (~inject, model: Model.t) => {
  let cardstack = model |> Model.get_cardstack;
  let show_prev = Cardstack.has_prev(cardstack) ? [] : [Vdom.Attr.disabled];
  Vdom.(
    Node.button(
      [
        Attr.id("cardstack-prev-button"),
        Attr.on_click(_ => inject(ModelAction.PrevCard)),
        ...show_prev,
      ],
      [Node.text("Previous")],
    )
  );
};

let next_card_button = (~inject, model: Model.t) => {
  let cardstack = model |> Model.get_cardstack;
  let show_next = Cardstack.has_next(cardstack) ? [] : [Vdom.Attr.disabled];
  Vdom.(
    Node.button(
      [
        Attr.id("cardstack-next-button"),
        Attr.on_click(_ => inject(ModelAction.NextCard)),
        ...show_next,
      ],
      [Node.text("Next")],
    )
  );
};

let cardstack_controls = (~inject, model: Model.t) =>
  Vdom.(
    Node.div(
      [Attr.id("cardstack-controls")],
      [
        Node.div(
          [Attr.id("button-centering-container")],
          [
            prev_card_button(~inject, model),
            next_card_button(~inject, model),
          ],
        ),
      ],
    )
  );

let view = (~inject: ModelAction.t => Vdom.Event.t, model: Model.t) => {
  let settings = model.settings;
  TimeUtil.measure_time(
    "Page.view",
    settings.performance.measure && settings.performance.page_view,
    () => {
      open Vdom;
      let card = model |> Model.get_card;
      let program = model |> Model.get_program;
      let selected_instance = model |> Model.get_selected_hole_instance;
      let cell_status =
        if (!settings.evaluation.evaluate) {
          Node.div([], []);
        } else {
          Node.div(
            [],
            [
              Node.div(
                [Attr.classes(["cell-status"])],
                [
                  Node.div(
                    [Attr.classes(["type-indicator"])],
                    [
                      Node.div(
                        [Attr.classes(["type-label"])],
                        [Node.text("Result of type: ")],
                      ),
                      Node.div(
                        [Attr.classes(["htype-view"])],
                        [
                          {
                            let (_, ty, _) = program.edit_state;
                            HTypCode.view(ty);
                          },
                        ],
                      ),
                    ],
                  ),
                ],
              ),
              Node.div(
                [Attr.classes(["result-view"])],
                [
                  DHCode.view(
                    ~inject,
                    ~selected_instance,
                    ~settings=settings.evaluation,
                    ~width=80,
                    ~font_metrics=model.font_metrics,
                    settings.evaluation.show_unevaluated_expansion
                      ? program |> Program.get_expansion
                      : program |> Program.get_result |> Result.get_dhexp,
                  ),
                ],
              ),
            ],
          );
        };
      Node.div(
        [Attr.id("root")],
        [
          Node.div(
            [Attr.classes(["top-bar"])],
            [
              Node.a(
                [
                  Attr.classes(["logo-text"]),
                  Attr.href("https://hazel.org"),
                ],
                [Node.text("Hazel")],
              ),
              cardstacks_select(~inject, Model.cardstack_info),
            ],
          ),
          Node.div(
            [Attr.classes(["main-area"])],
            [
              Sidebar.left(~inject, ~is_open=model.left_sidebar_open, () =>
                [ActionPanel.view(~inject, model)]
              ),
              Node.div(
                [Attr.classes(["flex-wrapper"])],
                [
                  Node.div(
                    [Attr.id("page-area")],
                    [
                      Node.div(
                        [Attr.classes(["page"])],
                        [
                          Node.div(
                            [Attr.classes(["card-caption"])],
                            [card.info.caption],
                          ),
                          Cell.view(~inject, model),
                          cell_status,
                          cardstack_controls(~inject, model),
                        ],
                      ),
                      examples_select(~inject),
                      Node.button(
                        [
                          Attr.on_click(_ => {
                            let e = program |> Program.get_uhexp;
                            JSUtil.log(
                              Js.string(Serialization.string_of_exp(e)),
                            );
                            Event.Ignore;
                          }),
                        ],
                        [Node.text("Serialize to console")],
                      ),
                      Node.div(
                        [
                          Attr.style(
                            Css_gen.(
                              white_space(`Pre) @> font_family(["monospace"])
                            ),
                          ),
                        ],
                        [],
                      ),
                    ],
                  ),
                ],
              ),
              Sidebar.right(~inject, ~is_open=model.right_sidebar_open, () =>
                [
                  CursorInspector.view(~inject, model),
                  ContextInspector.view(
                    ~inject,
                    ~selected_instance,
                    ~settings=settings.evaluation,
                    ~font_metrics=model.font_metrics,
                    program,
                  ),
                  UndoHistoryPanel.view(~inject, model),
                  SettingsPanel.view(~inject, settings),
                ]
              ),
            ],
          ),
        ],
      );
    },
  );
};
