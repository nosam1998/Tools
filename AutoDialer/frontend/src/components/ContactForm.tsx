import { useState } from "react";
import { useToast } from "@/components/ui/use-toast";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";

const ContactForm = () => {
  const { toast } = useToast();
  const [formData, setFormData] = useState({
    url: "",
    phone: "",
  });
  const [errors, setErrors] = useState({
    url: "",
    phone: "",
  });

  const validateURL = (url: string) => {
    try {
      new URL(url);
      return true;
    } catch {
      return false;
    }
  };

  const validatePhone = (phone: string) => {
    const phoneRegex = /^\+?[1-9]\d{1,14}$/;
    return phoneRegex.test(phone.replace(/[\s-]/g, ""));
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const newErrors = {
      url: !validateURL(formData.url) ? "Please enter a valid URL" : "",
      phone: !validatePhone(formData.phone)
        ? "Please enter a valid phone number"
        : "",
    };

    setErrors(newErrors);

    // Form validation successful
    if (!newErrors.url && !newErrors.phone) {
      const server = process.env.BACKEND_URL || "http://localhost:8000";
      fetch(`${server}/add_phone_number`, {
        method: "POST",
        body: JSON.stringify({
          google_maps_url: formData.url,
          phone_number: formData.phone,
        }),
      });
      toast({
        title: "Success!",
        description: "Form submitted successfully",
      });
      setFormData({ url: "", phone: "" });
    }
  };

  return (
    <div className="min-h-screen flex items-center justify-center bg-gradient-to-br from-gray-50 to-gray-100 p-4">
      <Card className="w-full max-w-2xl p-8 transition-all duration-300 hover:shadow-lg">
        <form onSubmit={handleSubmit} className="space-y-8">
          <h2 className="text-3xl font-semibold text-center mb-8 text-gray-800">
            Contact Information
          </h2>

          <div className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="url" className="text-sm font-medium">
                Website URL
              </Label>
              <Input
                id="url"
                type="text"
                placeholder="https://example.com"
                value={formData.url}
                onChange={(e) =>
                  setFormData({ ...formData, url: e.target.value })
                }
                className={cn(
                  "transition-all duration-200 focus:ring-2 focus:ring-blue-500",
                  errors.url && "border-red-500 focus:ring-red-500"
                )}
              />
              {errors.url && (
                <p className="text-red-500 text-sm mt-1">{errors.url}</p>
              )}
            </div>

            <div className="space-y-2">
              <Label htmlFor="phone" className="text-sm font-medium">
                Phone Number
              </Label>
              <Input
                id="phone"
                type="tel"
                placeholder="+1 (234) 567-8900"
                value={formData.phone}
                onChange={(e) =>
                  setFormData({ ...formData, phone: e.target.value })
                }
                className={cn(
                  "transition-all duration-200 focus:ring-2 focus:ring-blue-500",
                  errors.phone && "border-red-500 focus:ring-red-500"
                )}
              />
              {errors.phone && (
                <p className="text-red-500 text-sm mt-1">{errors.phone}</p>
              )}
            </div>
          </div>

          <Button
            type="submit"
            className="w-full py-6 text-lg font-medium transition-all duration-200 bg-gradient-to-r from-blue-500 to-blue-600 hover:from-blue-600 hover:to-blue-700"
          >
            Submit
          </Button>
        </form>
      </Card>
    </div>
  );
};

export default ContactForm;
