#include "boxes/proxy_subscription_box.h"

#include "core/proxy_subscription.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

object_ptr<Ui::BoxContent> ProxySubscriptionBox() {
	return Box([](not_null<Ui::GenericBox*> box) {
		auto &manager = Core::ProxySubscriptions();
		box->setTitle(rpl::single(u"Subscribtion"_q));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(u"Вставьте ссылку подписки. Рабочие серверы выбираются автоматически каждые 30 секунд, список обновляется раз в час. Этот режим меняет подключение только в vinoGram."_q),
			st::boxLabel));
		const auto url = box->addRow(object_ptr<Ui::InputField>(
			box, st::defaultInputField, Ui::InputField::Mode::SingleLine,
			rpl::single(u"https://…"_q), manager.url()));
		const auto enabled = box->addRow(object_ptr<Ui::Checkbox>(
			box, u"Использовать подписку"_q, manager.enabled()));
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box, rpl::single(manager.status()), st::boxLabel));
		const auto servers = box->addRow(object_ptr<Ui::FlatLabel>(
			box, rpl::single(manager.servers()), st::boxLabel));
		enabled->checkedChanges() | rpl::on_next([=](bool value) {
			Core::ProxySubscriptions().setEnabled(value);
		}, box->lifetime());
		const auto refresh = box->addButton(rpl::single(u"Обновить"_q), [=] {
			Core::ProxySubscriptions().refresh(url->getLastText());
		});
		refresh->setDisabled(manager.busy());
		manager.updates() | rpl::on_next([=] {
			auto &manager = Core::ProxySubscriptions();
			enabled->setChecked(manager.enabled());
			refresh->setDisabled(manager.busy());
			status->setText(manager.status());
			servers->setText(manager.servers());
		}, box->lifetime());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addLeftButton(rpl::single(u"Удалить"_q), [=] {
			Core::ProxySubscriptions().remove();
			url->setText(QString());
		});
	});
}
